#include "miku_msg_store.h"
#include "miku_log.h"
#include "miku_uuid.h"
#include "miku_common.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MK_MSG_MEM_CAP 8192

typedef struct {
    char    msg_id[64];
    char    conversation_id[128];
    char    sender_id[64];
    char    content[1024];
    int     content_type;
    int64_t send_time;
    int     status;
    int     used;
} mem_msg_t;

struct miku_msg_store_s {
    miku_mongo_t *mongo;
    int           enabled;   /* mongo backend */
    mem_msg_t    *mem;       /* always-on local ring for purge/cron */
    int           mem_count;
    int           mem_cap;
};

miku_msg_store_t *miku_msg_store_create(miku_mongo_t *mongo) {
    miku_msg_store_t *s = (miku_msg_store_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->mongo = mongo;
    s->enabled = (mongo != NULL);
    s->mem_cap = MK_MSG_MEM_CAP;
    s->mem = (mem_msg_t *)calloc((size_t)s->mem_cap, sizeof(mem_msg_t));
    if (!s->mem) { free(s); return NULL; }
    return s;
}

void miku_msg_store_destroy(miku_msg_store_t *store) {
    if (!store) return;
    free(store->mem);
    free(store);
}

static mem_msg_t *mem_alloc_slot(miku_msg_store_t *store) {
    for (int i = 0; i < store->mem_cap; i++) {
        if (!store->mem[i].used) {
            memset(&store->mem[i], 0, sizeof(store->mem[i]));
            store->mem[i].used = 1;
            store->mem_count++;
            return &store->mem[i];
        }
    }
    /* Evict oldest by send_time */
    int oldest = 0;
    for (int i = 1; i < store->mem_cap; i++) {
        if (store->mem[i].send_time < store->mem[oldest].send_time)
            oldest = i;
    }
    memset(&store->mem[oldest], 0, sizeof(store->mem[oldest]));
    store->mem[oldest].used = 1;
    return &store->mem[oldest];
}

static mem_msg_t *mem_find(miku_msg_store_t *store, const char *msg_id) {
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && strcmp(store->mem[i].msg_id, msg_id) == 0)
            return &store->mem[i];
    }
    return NULL;
}

int miku_msg_store_count(miku_msg_store_t *store) {
    return store ? store->mem_count : 0;
}

int miku_msg_store_insert(miku_msg_store_t *store, const char *conversation_id,
                           const char *sender_id, int content_type,
                           const char *content, int64_t send_time,
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
    m->status = 1;

    if (out_msg_id && msg_id_cap > 0)
        strncpy(out_msg_id, msg_id, msg_id_cap - 1);

    if (!store->enabled) {
        MK_LOG_DEBUG("msg_store: insert mem (conv=%s msg=%s)", conversation_id, msg_id);
        return 0;
    }

    char doc[4096];
    int n = snprintf(doc, sizeof(doc),
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%ld,\"status\":1}",
        msg_id, conversation_id, sender_id, content_type, content, (long)send_time);
    if (n < 0 || (size_t)n >= sizeof(doc)) return -1;

    return miku_mongo_insert(store->mongo, "messages", doc);
}

int miku_msg_store_find_by_conv(miku_msg_store_t *store, const char *conversation_id,
                                  int64_t start_seq, int64_t end_seq,
                                  char **results_json) {
    (void)start_seq; (void)end_seq;
    if (!store || !conversation_id) return -1;

    if (store->enabled) {
        char filter[256];
        snprintf(filter, sizeof(filter),
                 "{\"conversationID\":\"%s\",\"seq\":{\"$gte\":%ld,\"$lte\":%ld}}",
                 conversation_id, (long)start_seq, (long)end_seq);
        return miku_mongo_find_one(store->mongo, "messages", filter, results_json);
    }

    /* Build JSON array from memory */
    size_t cap = 4096;
    char *buf = (char *)malloc(cap);
    if (!buf) return -1;
    size_t pos = 0;
    buf[pos++] = '[';
    int first = 1;
    for (int i = 0; i < store->mem_cap; i++) {
        mem_msg_t *m = &store->mem[i];
        if (!m->used || strcmp(m->conversation_id, conversation_id) != 0) continue;
        char item[1536];
        int n = snprintf(item, sizeof(item),
            "%s{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
            "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"status\":%d}",
            first ? "" : ",",
            m->msg_id, m->conversation_id, m->sender_id,
            m->content_type, m->content, (long long)m->send_time, m->status);
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
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%lld,\"status\":%d}",
        m->msg_id, m->conversation_id, m->sender_id,
        m->content_type, m->content, (long long)m->send_time, m->status);
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
    if (m) {
        m->used = 0;
        store->mem_count--;
        if (store->mem_count < 0) store->mem_count = 0;
    }

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
            store->mem[i].used = 0;
            removed++;
        }
    }
    store->mem_count -= removed;
    if (store->mem_count < 0) store->mem_count = 0;
    MK_LOG_DEBUG("msg_store: purged %d msgs older than %lld", removed, (long long)cutoff_ms);
    return removed;
}

int miku_msg_store_clear_user(miku_msg_store_t *store, const char *user_id) {
    if (!store || !user_id) return -1;
    int removed = 0;
    for (int i = 0; i < store->mem_cap; i++) {
        if (store->mem[i].used && strcmp(store->mem[i].sender_id, user_id) == 0) {
            store->mem[i].used = 0;
            removed++;
        }
    }
    store->mem_count -= removed;
    if (store->mem_count < 0) store->mem_count = 0;
    return removed;
}
