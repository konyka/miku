#include "miku_object_store.h"
#include "miku_hash.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define MK_OBJ_CAP  4096
#define MK_OBJ_HASH 8192

typedef struct {
    miku_object_meta_t meta;
    int                used;
} obj_slot_t;

struct miku_object_store_s {
    obj_slot_t      *slots;
    int             *free_stack;
    int              free_top;
    int             *name_hash;
    int              count;
    int              evict_cursor;
    pthread_rwlock_t lock;
};

int miku_object_name_is_safe(const char *name) {
    if (!name || !name[0]) return 0;
    size_t n = 0;
    int prev_slash = 0;
    int seg_dots = 0;
    int seg_len = 0;
    for (; name[n]; n++) {
        if (n >= 255) return 0;
        unsigned char c = (unsigned char)name[n];
        if (c == '/') {
            if (n == 0 || prev_slash) return 0;
            if (seg_dots == seg_len && seg_len >= 1) return 0; /* "." or ".." */
            prev_slash = 1;
            seg_dots = 0;
            seg_len = 0;
            continue;
        }
        prev_slash = 0;
        if (c == '.') seg_dots++;
        seg_len++;
        if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-'))
            return 0;
    }
    if (prev_slash) return 0;
    if (seg_dots == seg_len && seg_len >= 1) return 0;
    return 1;
}

static uint32_t name_bucket(const char *name) {
    return (uint32_t)(miku_fnv1a_64(name, strlen(name)) & (MK_OBJ_HASH - 1));
}

static void name_hash_rebuild(miku_object_store_t *s) {
    for (int i = 0; i < MK_OBJ_HASH; i++) s->name_hash[i] = -1;
    for (int slot = 0; slot < MK_OBJ_CAP; slot++) {
        if (!s->slots[slot].used) continue;
        uint32_t h = name_bucket(s->slots[slot].meta.name);
        for (int n = 0; n < MK_OBJ_HASH; n++) {
            if (s->name_hash[h] < 0) {
                s->name_hash[h] = slot;
                break;
            }
            h = (h + 1) & (MK_OBJ_HASH - 1);
        }
    }
}

static int name_find(miku_object_store_t *s, const char *name) {
    uint32_t h = name_bucket(name);
    for (int n = 0; n < MK_OBJ_HASH; n++) {
        int slot = s->name_hash[h];
        if (slot < 0) return -1;
        if (s->slots[slot].used && strcmp(s->slots[slot].meta.name, name) == 0)
            return slot;
        h = (h + 1) & (MK_OBJ_HASH - 1);
    }
    return -1;
}

static int alloc_slot(miku_object_store_t *s) {
    int slot;
    if (s->free_top > 0) {
        slot = s->free_stack[--s->free_top];
    } else {
        slot = s->evict_cursor;
        s->evict_cursor = (s->evict_cursor + 1) % MK_OBJ_CAP;
        if (s->slots[slot].used) {
            s->slots[slot].used = 0;
            if (s->count > 0) s->count--;
            if (s->free_top < MK_OBJ_CAP) s->free_stack[s->free_top++] = slot;
            name_hash_rebuild(s);
        }
        slot = s->free_stack[--s->free_top];
    }
    memset(&s->slots[slot], 0, sizeof(s->slots[slot]));
    s->slots[slot].used = 1;
    s->count++;
    return slot;
}

static void free_slot(miku_object_store_t *s, int slot) {
    if (slot < 0 || slot >= MK_OBJ_CAP || !s->slots[slot].used) return;
    s->slots[slot].used = 0;
    if (s->count > 0) s->count--;
    if (s->free_top < MK_OBJ_CAP) s->free_stack[s->free_top++] = slot;
    name_hash_rebuild(s);
}

miku_object_store_t *miku_object_store_create(void) {
    miku_object_store_t *s = (miku_object_store_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (pthread_rwlock_init(&s->lock, NULL) != 0) {
        free(s);
        return NULL;
    }
    s->slots = (obj_slot_t *)calloc(MK_OBJ_CAP, sizeof(obj_slot_t));
    s->free_stack = (int *)malloc(MK_OBJ_CAP * sizeof(int));
    s->name_hash = (int *)malloc(MK_OBJ_HASH * sizeof(int));
    if (!s->slots || !s->free_stack || !s->name_hash) {
        free(s->slots);
        free(s->free_stack);
        free(s->name_hash);
        pthread_rwlock_destroy(&s->lock);
        free(s);
        return NULL;
    }
    for (int i = 0; i < MK_OBJ_HASH; i++) s->name_hash[i] = -1;
    for (int i = MK_OBJ_CAP - 1; i >= 0; i--)
        s->free_stack[s->free_top++] = i;
    return s;
}

void miku_object_store_destroy(miku_object_store_t *store) {
    if (!store) return;
    free(store->slots);
    free(store->free_stack);
    free(store->name_hash);
    pthread_rwlock_destroy(&store->lock);
    free(store);
}

int miku_object_store_upsert(miku_object_store_t *store, const miku_object_meta_t *in) {
    if (!store || !in || !miku_object_name_is_safe(in->name)) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int slot = name_find(store, in->name);
    if (slot < 0) slot = alloc_slot(store);
    store->slots[slot].meta = *in;
    store->slots[slot].meta.name[sizeof(store->slots[slot].meta.name) - 1] = '\0';
    store->slots[slot].used = 1;
    if (store->slots[slot].meta.created_ms <= 0)
        store->slots[slot].meta.created_ms = miku_timestamp_ms();
    name_hash_rebuild(store);
    pthread_rwlock_unlock(&store->lock);
    return 0;
}

int miku_object_store_get(miku_object_store_t *store, const char *name,
                          miku_object_meta_t *out) {
    if (!store || !name || !out) return -1;
    pthread_rwlock_rdlock(&store->lock);
    int slot = name_find(store, name);
    int rc = -1;
    if (slot >= 0) {
        *out = store->slots[slot].meta;
        rc = 0;
    }
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_object_store_delete(miku_object_store_t *store, const char *name) {
    if (!store || !name) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int slot = name_find(store, name);
    if (slot >= 0) free_slot(store, slot);
    pthread_rwlock_unlock(&store->lock);
    return 0;
}

int miku_object_store_purge_expired(miku_object_store_t *store,
                                    int64_t now_ms, int64_t cutoff_ms) {
    if (!store) return -1;
    if (now_ms <= 0) now_ms = miku_timestamp_ms();
    pthread_rwlock_wrlock(&store->lock);
    int removed = 0;
    for (int i = 0; i < MK_OBJ_CAP; i++) {
        if (!store->slots[i].used) continue;
        const miku_object_meta_t *m = &store->slots[i].meta;
        int expired = (m->expire_ms > 0 && m->expire_ms < now_ms);
        int aged = (cutoff_ms > 0 && m->created_ms > 0 && m->created_ms < cutoff_ms);
        if (expired || aged) {
            store->slots[i].used = 0;
            if (store->count > 0) store->count--;
            if (store->free_top < MK_OBJ_CAP) store->free_stack[store->free_top++] = i;
            removed++;
        }
    }
    if (removed) name_hash_rebuild(store);
    pthread_rwlock_unlock(&store->lock);
    return removed;
}

int miku_object_store_count(miku_object_store_t *store) {
    if (!store) return 0;
    pthread_rwlock_rdlock(&store->lock);
    int n = store->count;
    pthread_rwlock_unlock(&store->lock);
    return n;
}
