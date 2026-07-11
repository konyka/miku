#include "miku_msg_store.h"
#include "miku_log.h"
#include "miku_uuid.h"
#include "miku_common.h"
#include "miku_hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MK_MSG_MEM_CAP 8192
#define MK_MSG_ID_HASH 16384  /* power of 2 */

typedef struct {
    char    msg_id[64];
    char    conversation_id[128];
    char    sender_id[64];
    char    content[1024];
    int     content_type;
    int64_t send_time;
    int64_t seq;
    int     status;
    int     used;
} mem_msg_t;

struct miku_msg_store_s {
    miku_mongo_t *mongo;
    int           enabled;
    mem_msg_t    *mem;
    int           mem_count;
    int           mem_cap;
    int          *free_stack;   /* unused slot indices */
    int           free_top;
    int          *id_hash;      /* msg_id hash → slot, -1 empty */
};

miku_msg_store_t *miku_msg_store_create(miku_mongo_t *mongo) {
    miku_msg_store_t *s = (miku_msg_store_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->mongo = mongo;
    s->enabled = (mongo != NULL);
    s->mem_cap = MK_MSG_MEM_CAP;
    s->mem = (mem_msg_t *)calloc((size_t)s->mem_cap, sizeof(mem_msg_t));
    s->free_stack = (int *)malloc((size_t)s->mem_cap * sizeof(int));
    s->id_hash = (int *)malloc(MK_MSG_ID_HASH * sizeof(int));
    if (!s->mem || !s->free_stack || !s->id_hash) {
        free(s->mem);
        free(s->free_stack);
        free(s->id_hash);
        free(s);
        return NULL;
    }
    for (int i = 0; i < MK_MSG_ID_HASH; i++) s->id_hash[i] = -1;
    s->free_top = 0;
    for (int i = s->mem_cap - 1; i >= 0; i--)
        s->free_stack[s->free_top++] = i;
    return s;
}

void miku_msg_store_destroy(miku_msg_store_t *store) {
    if (!store) return;
    free(store->mem);
    free(store->free_stack);
    free(store->id_hash);
    free(store);
}

static uint32_t id_bucket(const char *msg_id) {
    return (uint32_t)(miku_fnv1a_64(msg_id, strlen(msg_id)) & (MK_MSG_ID_HASH - 1));
}

static void id_hash_put(miku_msg_store_t *store, const char *msg_id, int slot) {
    uint32_t b = id_bucket(msg_id);
    for (uint32_t i = 0; i < MK_MSG_ID_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_ID_HASH - 1);
        int cur = store->id_hash[idx];
        if (cur < 0 || (store->mem[cur].used &&
                        strcmp(store->mem[cur].msg_id, msg_id) == 0)) {
            store->id_hash[idx] = slot;
            return;
        }
    }
}

static void id_hash_del(miku_msg_store_t *store, const char *msg_id) {
    uint32_t b = id_bucket(msg_id);
    for (uint32_t i = 0; i < MK_MSG_ID_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_ID_HASH - 1);
        int cur = store->id_hash[idx];
        if (cur < 0) return;
        if (store->mem[cur].used && strcmp(store->mem[cur].msg_id, msg_id) == 0) {
            store->id_hash[idx] = -1;
            /* rehash cluster */
            uint32_t j = (idx + 1) & (MK_MSG_ID_HASH - 1);
            while (store->id_hash[j] >= 0) {
                int slot = store->id_hash[j];
                store->id_hash[j] = -1;
                id_hash_put(store, store->mem[slot].msg_id, slot);
                j = (j + 1) & (MK_MSG_ID_HASH - 1);
            }
            return;
        }
    }
}

static mem_msg_t *mem_find(miku_msg_store_t *store, const char *msg_id) {
    uint32_t b = id_bucket(msg_id);
    for (uint32_t i = 0; i < MK_MSG_ID_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_ID_HASH - 1);
        int cur = store->id_hash[idx];
        if (cur < 0) return NULL;
        if (store->mem[cur].used && strcmp(store->mem[cur].msg_id, msg_id) == 0)
            return &store->mem[cur];
    }
    return NULL;
}

static void mem_free_slot(miku_msg_store_t *store, int slot) {
    if (slot < 0 || slot >= store->mem_cap || !store->mem[slot].used) return;
    id_hash_del(store, store->mem[slot].msg_id);
    store->mem[slot].used = 0;
    store->mem_count--;
    if (store->mem_count < 0) store->mem_count = 0;
    if (store->free_top < store->mem_cap)
        store->free_stack[store->free_top++] = slot;
}

static mem_msg_t *mem_alloc_slot(miku_msg_store_t *store) {
    int slot;
    if (store->free_top > 0) {
        slot = store->free_stack[--store->free_top];
    } else {
        /* Evict oldest by send_time — rare path when full */
        int oldest = 0;
        for (int i = 1; i < store->mem_cap; i++) {
            if (store->mem[i].send_time < store->mem[oldest].send_time)
                oldest = i;
        }
        mem_free_slot(store, oldest);
        slot = store->free_stack[--store->free_top];
    }
    memset(&store->mem[slot], 0, sizeof(store->mem[slot]));
    store->mem[slot].used = 1;
    store->mem_count++;
    return &store->mem[slot];
}

static int mem_slot_of(miku_msg_store_t *store, mem_msg_t *m) {
    return (int)(m - store->mem);
}

int miku_msg_store_count(miku_msg_store_t *store) {
    return store ? store->mem_count : 0;
}

int miku_msg_store_insert(miku_msg_store_t *store, const char *conversation_id,
                           const char *sender_id, int content_type,
                           const char *content, int64_t send_time, int64_t seq,
                           char *out_msg_id, size_t msg_id_cap) {
    if (!store || !conversation_id || !sender_id || !content) return -1;

    char msg_id[64] = {0};
    miku_uuid_generate(msg_id);

    mem_msg_t *m = mem_alloc_slot(store);
    strncpy(m->msg_id, msg_id, sizeof(m->msg_id) - 1);
    strncpy(m->conversation_id, conversation_id, sizeof(m->conversation_id) - 1);
    strncpy(m->sender_id, sender_id, sizeof(m->sender_id) - 1);
    strncpy(m->content, content, sizeof(m->content) - 1);
    m->content_type = content_type;
    m->send_time = send_time > 0 ? send_time : miku_timestamp_ms();
    m->seq = seq;
    m->status = 1;
    id_hash_put(store, m->msg_id, mem_slot_of(store, m));

    if (out_msg_id && msg_id_cap > 0)
        strncpy(out_msg_id, msg_id, msg_id_cap - 1);

    if (!store->enabled) {
        MK_LOG_DEBUG("msg_store: insert mem (conv=%s msg=%s seq=%lld)",
                     conversation_id, msg_id, (long long)seq);
        return 0;
    }

    char doc[4096];
    int n = snprintf(doc, sizeof(doc),
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%ld,\"seq\":%lld,\"status\":1}",
        msg_id, conversation_id, sender_id, content_type, content,
        (long)send_time, (long long)seq);
    if (n < 0 || (size_t)n >= sizeof(doc)) return -1;

    return miku_mongo_insert(store->mongo, "messages", doc);
}

int miku_msg_store_find_by_conv(miku_msg_store_t *store, const char *conversation_id,
                                  int64_t start_seq, int64_t end_seq,
                                  char **results_json) {
    if (!store || !conversation_id) return -1;

    if (store->enabled) {
        char filter[256];
        snprintf(filter, sizeof(filter),
                 "{\"conversationID\":\"%s\",\"seq\":{\"$gte\":%ld,\"$lte\":%ld}}",
                 conversation_id, (long)start_seq, (long)end_seq);
        return miku_mongo_find_one(store->mongo, "messages", filter, results_json);
    }

    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;
    size_t pos = 0;
    buf[pos++] = '[';
    int first = 1;
    for (int i = 0; i < store->mem_cap; i++) {
        mem_msg_t *m = &store->mem[i];
        if (!m->used || strcmp(m->conversation_id, conversation_id) != 0) continue;
        if (m->seq < start_seq) continue;
        if (end_seq > 0 && m->seq > end_seq) continue;
        char item[1536];
        int n = snprintf(item, sizeof(item),
            "%s{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
            "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"seq\":%lld,\"status\":%d}",
            first ? "" : ",",
            m->msg_id, m->conversation_id, m->sender_id,
            m->content_type, m->content, (long long)m->send_time,
            (long long)m->seq, m->status);
        if (n < 0) continue;
        if (pos + (size_t)n + 2 > cap) {
            size_t ncap = cap * 2;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) { free(buf); return -1; }
            buf = nb;
            cap = ncap;
        }
        memcpy(buf + pos, item, (size_t)n);
        pos += (size_t)n;
        first = 0;
    }
    buf[pos++] = ']';
    buf[pos] = '\0';
    if (results_json) *results_json = buf;
    else free(buf);
    return 0;
}

int miku_msg_store_find_one(miku_msg_store_t *store, const char *msg_id,
                              char **result_json) {
    if (!store || !msg_id) return -1;

    if (store->enabled) {
        char filter[128];
        snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
        return miku_mongo_find_one(store->mongo, "messages", filter, result_json);
    }

    mem_msg_t *m = mem_find(store, msg_id);
    if (!m) {
        if (result_json) *result_json = strdup("{}");
        return 0;
    }
    char *buf = (char *)malloc(1536);
    if (!buf) return -1;
    snprintf(buf, 1536,
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"seq\":%lld,\"status\":%d}",
        m->msg_id, m->conversation_id, m->sender_id,
        m->content_type, m->content, (long long)m->send_time,
        (long long)m->seq, m->status);
    if (result_json) *result_json = buf;
    else free(buf);
    return 0;
}

int miku_msg_store_update_status(miku_msg_store_t *store, const char *msg_id, int status) {
    if (!store || !msg_id) return -1;
    mem_msg_t *m = mem_find(store, msg_id);
    if (m) m->status = status;

    if (!store->enabled) return 0;

    char filter[128], update[128];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
    snprintf(update, sizeof(update), "{\"$set\":{\"status\":%d}}", status);
    return miku_mongo_update(store->mongo, "messages", filter, update, false);
}

int miku_msg_store_delete(miku_msg_store_t *store, const char *msg_id) {
    if (!store || !msg_id) return -1;
    mem_msg_t *m = mem_find(store, msg_id);
    if (m) mem_free_slot(store, mem_slot_of(store, m));

    if (!store->enabled) return 0;

    char filter[128];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
    return miku_mongo_delete(store->mongo, "messages", filter);
}

int miku_msg_store_purge_older_than(miku_msg_store_t *store, int64_t cutoff_ms) {
    if (!store) return -1;
    int removed = 0;
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && store->mem[i].send_time < cutoff_ms) {
            mem_free_slot(store, i);
            removed++;
        }
    }
    MK_LOG_DEBUG("msg_store: purged %d msgs older than %lld", removed, (long long)cutoff_ms);
    return removed;
}

int miku_msg_store_clear_user(miku_msg_store_t *store, const char *user_id) {
    if (!store || !user_id) return -1;
    int removed = 0;
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && strcmp(store->mem[i].sender_id, user_id) == 0) {
            mem_free_slot(store, i);
            removed++;
        }
    }
    return removed;
}
