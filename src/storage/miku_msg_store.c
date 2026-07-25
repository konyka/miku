#include "miku_msg_store.h"
#include <pthread.h>
#include "miku_log.h"
#include "miku_uuid.h"
#include "miku_common.h"
#include "miku_hash.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MK_MSG_MEM_CAP 8192
#define MK_MSG_ID_HASH 16384  /* power of 2 */
#define MK_MSG_CONV_HASH 16384

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
    int          *conv_hash;    /* conversation_id → head slot */
    int          *conv_next;    /* intrusive list within a conversation */
    int           evict_cursor; /* next slot to overwrite once the ring is full */
    /* miku-msggateway inserts from two threads: the WS event loop for SEND_MSG
     * and the admin HTTP thread for /internal/push_msg (both via
     * miku_msggw_ws_deliver_msg). mem_alloc_slot hands out slots with
     * `free_stack[--free_top]`, which is not atomic, so two concurrent inserts
     * could take the same slot -- the second message overwrites the first while
     * both callers get errCode 0 and a valid serverMsgID, and the first id then
     * resolves to the second message's content. Writes are frequent here, but
     * reads (pull-by-seq, pull-by-id) are more so, hence a rwlock. */
    pthread_rwlock_t lock;
};

miku_msg_store_t *miku_msg_store_create(miku_mongo_t *mongo) {
    miku_msg_store_t *s = (miku_msg_store_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (pthread_rwlock_init(&s->lock, NULL) != 0) {
        free(s);
        return NULL;
    }
    s->mongo = mongo;
    s->enabled = (mongo != NULL);
    s->mem_cap = MK_MSG_MEM_CAP;
    s->mem = (mem_msg_t *)calloc((size_t)s->mem_cap, sizeof(mem_msg_t));
    s->free_stack = (int *)malloc((size_t)s->mem_cap * sizeof(int));
    s->id_hash = (int *)malloc(MK_MSG_ID_HASH * sizeof(int));
    s->conv_hash = (int *)malloc(MK_MSG_CONV_HASH * sizeof(int));
    s->conv_next = (int *)malloc((size_t)s->mem_cap * sizeof(int));
    if (!s->mem || !s->free_stack || !s->id_hash || !s->conv_hash || !s->conv_next) {
        free(s->mem);
        free(s->free_stack);
        free(s->id_hash);
        free(s->conv_hash);
        free(s->conv_next);
        free(s);
        return NULL;
    }
    for (int i = 0; i < MK_MSG_ID_HASH; i++) s->id_hash[i] = -1;
    for (int i = 0; i < MK_MSG_CONV_HASH; i++) s->conv_hash[i] = -1;
    for (int i = 0; i < s->mem_cap; i++) s->conv_next[i] = -1;
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
    free(store->conv_hash);
    free(store->conv_next);
    pthread_rwlock_destroy(&store->lock);
    free(store);
}

static uint32_t id_bucket(const char *msg_id) {
    return (uint32_t)(miku_fnv1a_64(msg_id, strlen(msg_id)) & (MK_MSG_ID_HASH - 1));
}

static uint32_t conv_bucket(const char *conversation_id) {
    return (uint32_t)(miku_fnv1a_64(conversation_id, strlen(conversation_id)) & (MK_MSG_CONV_HASH - 1));
}

static void conv_link(miku_msg_store_t *store, int slot) {
    const char *cid = store->mem[slot].conversation_id;
    uint32_t b = conv_bucket(cid);
    for (uint32_t i = 0; i < MK_MSG_CONV_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_CONV_HASH - 1);
        int head = store->conv_hash[idx];
        if (head < 0) {
            store->conv_hash[idx] = slot;
            store->conv_next[slot] = -1;
            return;
        }
        if (store->mem[head].used &&
            strcmp(store->mem[head].conversation_id, cid) == 0) {
            store->conv_next[slot] = head;
            store->conv_hash[idx] = slot;
            return;
        }
    }
}

static void conv_unlink(miku_msg_store_t *store, int slot) {
    if (slot < 0 || slot >= store->mem_cap || !store->mem[slot].used) return;
    const char *cid = store->mem[slot].conversation_id;
    if (!cid[0]) {
        store->conv_next[slot] = -1;
        return;
    }
    uint32_t b = conv_bucket(cid);
    for (uint32_t i = 0; i < MK_MSG_CONV_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_CONV_HASH - 1);
        int head = store->conv_hash[idx];
        if (head < 0) return;
        if (!(store->mem[head].used &&
              strcmp(store->mem[head].conversation_id, cid) == 0))
            continue;
        if (head == slot) {
            store->conv_hash[idx] = store->conv_next[slot];
            store->conv_next[slot] = -1;
            if (store->conv_hash[idx] < 0) {
                uint32_t j = (idx + 1) & (MK_MSG_CONV_HASH - 1);
                while (store->conv_hash[j] >= 0) {
                    int rem = store->conv_hash[j];
                    store->conv_hash[j] = -1;
                    if (store->mem[rem].used) {
                        /* reinsert head for that conversation */
                        const char *rcid = store->mem[rem].conversation_id;
                        uint32_t rb = conv_bucket(rcid);
                        for (uint32_t k = 0; k < MK_MSG_CONV_HASH; k++) {
                            uint32_t ridx = (rb + k) & (MK_MSG_CONV_HASH - 1);
                            if (store->conv_hash[ridx] < 0) {
                                store->conv_hash[ridx] = rem;
                                break;
                            }
                        }
                    }
                    j = (j + 1) & (MK_MSG_CONV_HASH - 1);
                }
            }
            return;
        }
        int prev = head;
        for (int cur = store->conv_next[head]; cur >= 0;
             prev = cur, cur = store->conv_next[cur]) {
            if (cur == slot) {
                store->conv_next[prev] = store->conv_next[slot];
                store->conv_next[slot] = -1;
                return;
            }
        }
        return;
    }
}

static void rebuild_conv_chains(miku_msg_store_t *store) {
    for (int i = 0; i < MK_MSG_CONV_HASH; i++) store->conv_hash[i] = -1;
    for (int i = 0; i < store->mem_cap; i++) store->conv_next[i] = -1;
    for (int slot = 0; slot < store->mem_cap; slot++) {
        if (store->mem[slot].used) conv_link(store, slot);
    }
}

static int conv_head(miku_msg_store_t *store, const char *conversation_id) {
    uint32_t b = conv_bucket(conversation_id);
    for (uint32_t i = 0; i < MK_MSG_CONV_HASH; i++) {
        uint32_t idx = (b + i) & (MK_MSG_CONV_HASH - 1);
        int head = store->conv_hash[idx];
        if (head < 0) return -1;
        if (store->mem[head].used &&
            strcmp(store->mem[head].conversation_id, conversation_id) == 0)
            return head;
    }
    return -1;
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

static void mem_free_slot_raw(miku_msg_store_t *store, int slot) {
    if (slot < 0 || slot >= store->mem_cap || !store->mem[slot].used) return;
    id_hash_del(store, store->mem[slot].msg_id);
    store->mem[slot].used = 0;
    store->mem_count--;
    if (store->mem_count < 0) store->mem_count = 0;
    if (store->free_top < store->mem_cap)
        store->free_stack[store->free_top++] = slot;
}

static void mem_free_slot(miku_msg_store_t *store, int slot) {
    if (slot < 0 || slot >= store->mem_cap || !store->mem[slot].used) return;
    conv_unlink(store, slot);
    mem_free_slot_raw(store, slot);
}

static mem_msg_t *mem_alloc_slot(miku_msg_store_t *store) {
    int slot;
    if (store->free_top > 0) {
        slot = store->free_stack[--store->free_top];
    } else {
        /* Evict in insertion order with a rotating cursor. This is not the rare
         * path the previous comment assumed: once the ring is full it stays full,
         * so *every* insert took it, and it scanned all mem_cap slots for the
         * lowest send_time — 670 ns/insert became 16.9 us, permanently, after the
         * first 8192 messages (minutes of traffic). Now that inserts hold the
         * write lock, that scan also blocks every concurrent read. FIFO is the
         * same heuristic for a bounded ring — messages arrive in roughly send_time
         * order, so the oldest slot is the next one to be overwritten anyway — and
         * it is O(1). */
        slot = store->evict_cursor;
        store->evict_cursor = (store->evict_cursor + 1) % store->mem_cap;
        mem_free_slot(store, slot);
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

static int miku_msg_store_count_nolock(miku_msg_store_t *store) {
    return store ? store->mem_count : 0;
}

static int miku_msg_store_insert_nolock(miku_msg_store_t *store, const char *conversation_id,
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
    conv_link(store, mem_slot_of(store, m));

    if (out_msg_id && msg_id_cap > 0)
        strncpy(out_msg_id, msg_id, msg_id_cap - 1);

    if (!store->enabled) {
        MK_LOG_DEBUG("msg_store: insert mem (conv=%s msg=%s seq=%lld)",
                     conversation_id, msg_id, (long long)seq);
        return 0;
    }

    char e_mid[128], e_cid[256], e_sid[128], e_ct[2048];
    miku_json_escape_str(msg_id, e_mid, sizeof(e_mid));
    miku_json_escape_str(conversation_id, e_cid, sizeof(e_cid));
    miku_json_escape_str(sender_id, e_sid, sizeof(e_sid));
    miku_json_escape_str(content, e_ct, sizeof(e_ct));
    char doc[4096];
    int n = snprintf(doc, sizeof(doc),
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%ld,\"seq\":%lld,\"status\":1}",
        e_mid, e_cid, e_sid, content_type, e_ct,
        (long)send_time, (long long)seq);
    if (n < 0 || (size_t)n >= sizeof(doc)) return -1;

    return miku_mongo_insert(store->mongo, "messages", doc);
}

static int miku_msg_store_find_by_conv_nolock(miku_msg_store_t *store, const char *conversation_id,
                                  int64_t start_seq, int64_t end_seq,
                                  char **results_json) {
    if (!store || !conversation_id) return -1;

    if (store->enabled) {
        char ecid[256]; miku_json_escape_str(conversation_id, ecid, sizeof(ecid));
        char filter[512];
        snprintf(filter, sizeof(filter),
                 "{\"conversationID\":\"%s\",\"seq\":{\"$gte\":%ld,\"$lte\":%ld}}",
                 ecid, (long)start_seq, (long)end_seq);
        return miku_mongo_find_one(store->mongo, "messages", filter, results_json);
    }

    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;
    size_t pos = 0;
    buf[pos++] = '[';
    int first = 1;
    for (int slot = conv_head(store, conversation_id); slot >= 0; slot = store->conv_next[slot]) {
        mem_msg_t *m = &store->mem[slot];
        if (!m->used) continue;
        if (m->seq < start_seq) continue;
        if (end_seq > 0 && m->seq > end_seq) continue;
        char item[2048];
        char e_mid[128], e_cid[256], e_sid[128], e_ct[1100];
        miku_json_escape_str(m->msg_id, e_mid, sizeof(e_mid));
        miku_json_escape_str(m->conversation_id, e_cid, sizeof(e_cid));
        miku_json_escape_str(m->sender_id, e_sid, sizeof(e_sid));
        miku_json_escape_str(m->content, e_ct, sizeof(e_ct));
        int n = snprintf(item, sizeof(item),
            "%s{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
            "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"seq\":%lld,\"status\":%d}",
            first ? "" : ",",
            e_mid, e_cid, e_sid,
            m->content_type, e_ct, (long long)m->send_time,
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

static int miku_msg_store_find_one_nolock(miku_msg_store_t *store, const char *msg_id,
                              char **result_json) {
    if (!store || !msg_id) return -1;

    if (store->enabled) {
        char eid[256]; miku_json_escape_str(msg_id, eid, sizeof(eid));
        char filter[288];
        snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", eid);
        return miku_mongo_find_one(store->mongo, "messages", filter, result_json);
    }

    mem_msg_t *m = mem_find(store, msg_id);
    if (!m) {
        if (result_json) *result_json = strdup("{}");
        return 0;
    }
    char e_mid[128], e_cid[256], e_sid[128], e_ct[2048];
    miku_json_escape_str(m->msg_id, e_mid, sizeof(e_mid));
    miku_json_escape_str(m->conversation_id, e_cid, sizeof(e_cid));
    miku_json_escape_str(m->sender_id, e_sid, sizeof(e_sid));
    miku_json_escape_str(m->content, e_ct, sizeof(e_ct));
    char *buf = (char *)malloc(4096);
    if (!buf) return -1;
    snprintf(buf, 4096,
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"seq\":%lld,\"status\":%d}",
        e_mid, e_cid, e_sid,
        m->content_type, e_ct, (long long)m->send_time,
        (long long)m->seq, m->status);
    if (result_json) *result_json = buf;
    else free(buf);
    return 0;
}

static int miku_msg_store_update_status_nolock(miku_msg_store_t *store, const char *msg_id, int status) {
    if (!store || !msg_id) return -1;
    mem_msg_t *m = mem_find(store, msg_id);
    if (m) m->status = status;

    if (!store->enabled) return 0;

    char filter[288], update[128];
    char eid[256]; miku_json_escape_str(msg_id, eid, sizeof(eid));
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", eid);
    snprintf(update, sizeof(update), "{\"$set\":{\"status\":%d}}", status);
    return miku_mongo_update(store->mongo, "messages", filter, update, false);
}

static int miku_msg_store_delete_nolock(miku_msg_store_t *store, const char *msg_id) {
    if (!store || !msg_id) return -1;
    mem_msg_t *m = mem_find(store, msg_id);
    if (m) mem_free_slot(store, mem_slot_of(store, m));

    if (!store->enabled) return 0;

    char eid[256]; miku_json_escape_str(msg_id, eid, sizeof(eid));
    char filter[288];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", eid);
    return miku_mongo_delete(store->mongo, "messages", filter);
}

static int miku_msg_store_purge_older_than_nolock(miku_msg_store_t *store, int64_t cutoff_ms) {
    if (!store) return -1;
    int removed = 0;
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && store->mem[i].send_time < cutoff_ms) {
            mem_free_slot_raw(store, i);
            removed++;
        }
    }
    if (removed) rebuild_conv_chains(store);
    MK_LOG_DEBUG("msg_store: purged %d msgs older than %lld", removed, (long long)cutoff_ms);
    return removed;
}

static int miku_msg_store_clear_user_nolock(miku_msg_store_t *store, const char *user_id) {
    if (!store || !user_id) return -1;
    int removed = 0;
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && strcmp(store->mem[i].sender_id, user_id) == 0) {
            mem_free_slot_raw(store, i);
            removed++;
        }
    }
    if (removed) rebuild_conv_chains(store);
    return removed;
}

/* Locked public entry points over the _nolock internals above. */

int miku_msg_store_count(miku_msg_store_t *store) {
    if (!store) return 0;
    pthread_rwlock_rdlock(&store->lock);
    int rc = miku_msg_store_count_nolock(store);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_insert(miku_msg_store_t *store, const char *conversation_id,
                           const char *sender_id, int content_type,
                           const char *content, int64_t send_time, int64_t seq,
                           char *out_msg_id, size_t msg_id_cap) {
    if (!store) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int rc = miku_msg_store_insert_nolock(store, conversation_id, sender_id, content_type, content, send_time, seq, out_msg_id, msg_id_cap);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_find_by_conv(miku_msg_store_t *store, const char *conversation_id,
                                 int64_t start_seq, int64_t end_seq,
                                 char **results_json) {
    if (!store) return -1;
    pthread_rwlock_rdlock(&store->lock);
    int rc = miku_msg_store_find_by_conv_nolock(store, conversation_id, start_seq, end_seq, results_json);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_find_one(miku_msg_store_t *store, const char *msg_id,
                             char **result_json) {
    if (!store) return -1;
    pthread_rwlock_rdlock(&store->lock);
    int rc = miku_msg_store_find_one_nolock(store, msg_id, result_json);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_update_status(miku_msg_store_t *store, const char *msg_id,
                                  int status) {
    if (!store) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int rc = miku_msg_store_update_status_nolock(store, msg_id, status);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_delete(miku_msg_store_t *store, const char *msg_id) {
    if (!store) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int rc = miku_msg_store_delete_nolock(store, msg_id);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_purge_older_than(miku_msg_store_t *store, int64_t cutoff_ms) {
    if (!store) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int rc = miku_msg_store_purge_older_than_nolock(store, cutoff_ms);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}

int miku_msg_store_clear_user(miku_msg_store_t *store, const char *user_id) {
    if (!store) return -1;
    pthread_rwlock_wrlock(&store->lock);
    int rc = miku_msg_store_clear_user_nolock(store, user_id);
    pthread_rwlock_unlock(&store->lock);
    return rc;
}
