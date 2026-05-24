#include "miku_hashmap.h"
#include <stdlib.h>
#include <string.h>

#define HM_EMPTY    0
#define HM_OCCUPIED 1
#define HM_DELETED  2

typedef struct {
    char   *key;
    void   *val;
    int     state;
} miku_hm_entry_t;

struct miku_hashmap_s {
    miku_hm_entry_t *entries;
    size_t           cap;
    size_t           count;
    miku_free_fn     val_free;
};

static uint64_t fnv1a(const char *key) {
    uint64_t h = 14695981039346656037ULL;
    while (*key) {
        h ^= (uint64_t)(unsigned char)(*key++);
        h *= 1099511628211ULL;
    }
    return h;
}

static int hm_resize(miku_hashmap_t *map) {
    size_t newcap = map->cap * 2;
    miku_hm_entry_t *ne = (miku_hm_entry_t *)calloc(newcap, sizeof(*ne));
    if (!ne) return -1;
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[i].state != HM_OCCUPIED) continue;
        uint64_t h = fnv1a(map->entries[i].key) & (newcap - 1);
        while (ne[h].state == HM_OCCUPIED) h = (h + 1) & (newcap - 1);
        ne[h] = map->entries[i];
    }
    free(map->entries);
    map->entries = ne;
    map->cap = newcap;
    return 0;
}

miku_hashmap_t *miku_hashmap_create(size_t initial_cap, miku_free_fn val_free) {
    size_t cap = 16;
    while (cap < initial_cap) cap *= 2;
    miku_hashmap_t *m = (miku_hashmap_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->entries = (miku_hm_entry_t *)calloc(cap, sizeof(*m->entries));
    if (!m->entries) { free(m); return NULL; }
    m->cap = cap;
    m->val_free = val_free;
    return m;
}

void miku_hashmap_destroy(miku_hashmap_t *map) {
    if (!map) return;
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[i].state == HM_OCCUPIED) {
            free(map->entries[i].key);
            if (map->val_free && map->entries[i].val)
                map->val_free(map->entries[i].val);
        }
    }
    free(map->entries);
    free(map);
}

int miku_hashmap_put(miku_hashmap_t *map, const char *key, void *val) {
    if (!map || !key) return -1;
    if (map->count * 4 >= map->cap * 3) {
        if (hm_resize(map) != 0) return -1;
    }
    uint64_t h = fnv1a(key) & (map->cap - 1);
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[h].state == HM_OCCUPIED) {
            if (strcmp(map->entries[h].key, key) == 0) {
                if (map->val_free && map->entries[h].val)
                    map->val_free(map->entries[h].val);
                map->entries[h].val = val;
                return 0;
            }
        } else if (map->entries[h].state == HM_EMPTY) {
            break;
        }
        h = (h + 1) & (map->cap - 1);
    }

    h = fnv1a(key) & (map->cap - 1);
    while (map->entries[h].state == HM_OCCUPIED) {
        h = (h + 1) & (map->cap - 1);
    }
    if (map->entries[h].state == HM_DELETED) {
    }
    map->entries[h].key = strdup(key);
    map->entries[h].val = val;
    map->entries[h].state = HM_OCCUPIED;
    map->count++;
    return 0;
}

void *miku_hashmap_get(const miku_hashmap_t *map, const char *key) {
    if (!map || !key) return NULL;
    uint64_t h = fnv1a(key) & (map->cap - 1);
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[h].state == HM_EMPTY) return NULL;
        if (map->entries[h].state == HM_OCCUPIED &&
            strcmp(map->entries[h].key, key) == 0)
            return map->entries[h].val;
        h = (h + 1) & (map->cap - 1);
    }
    return NULL;
}

int miku_hashmap_del(miku_hashmap_t *map, const char *key) {
    if (!map || !key) return -1;
    uint64_t h = fnv1a(key) & (map->cap - 1);
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[h].state == HM_EMPTY) return -1;
        if (map->entries[h].state == HM_OCCUPIED &&
            strcmp(map->entries[h].key, key) == 0) {
            free(map->entries[h].key);
            if (map->val_free && map->entries[h].val)
                map->val_free(map->entries[h].val);
            map->entries[h].state = HM_DELETED;
            map->entries[h].key = NULL;
            map->entries[h].val = NULL;
            map->count--;
            return 0;
        }
        h = (h + 1) & (map->cap - 1);
    }
    return -1;
}

size_t miku_hashmap_size(const miku_hashmap_t *map) {
    return map ? map->count : 0;
}

void miku_hashmap_foreach(const miku_hashmap_t *map,
                           void (*fn)(const char *key, void *val, void *ctx),
                           void *ctx) {
    if (!map || !fn) return;
    for (size_t i = 0; i < map->cap; i++) {
        if (map->entries[i].state == HM_OCCUPIED)
            fn(map->entries[i].key, map->entries[i].val, ctx);
    }
}
