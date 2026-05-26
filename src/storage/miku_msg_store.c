#include "miku_msg_store.h"
#include "miku_log.h"
#include "miku_uuid.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct miku_msg_store_s {
    miku_mongo_t *mongo;
    int           enabled;
};

miku_msg_store_t *miku_msg_store_create(miku_mongo_t *mongo) {
    miku_msg_store_t *s = (miku_msg_store_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->mongo = mongo;
    s->enabled = (mongo != NULL);
    return s;
}

void miku_msg_store_destroy(miku_msg_store_t *store) {
    free(store);
}

int miku_msg_store_insert(miku_msg_store_t *store, const char *conversation_id,
                           const char *sender_id, int content_type,
                           const char *content, int64_t send_time,
                           char *out_msg_id, size_t msg_id_cap) {
    if (!store || !conversation_id || !sender_id || !content) return -1;

    char msg_id[64] = {0};
    miku_uuid_generate(msg_id);

    if (!store->enabled) {
        MK_LOG_DEBUG("msg_store: insert stub (conv=%s msg=%s)", conversation_id, msg_id);
        if (out_msg_id && msg_id_cap > 0)
            strncpy(out_msg_id, msg_id, msg_id_cap - 1);
        return 0;
    }

    char doc[4096];
    int n = snprintf(doc, sizeof(doc),
        "{\"msgID\":\"%s\",\"conversationID\":\"%s\",\"sendID\":\"%s\","
        "\"contentType\":%d,\"content\":\"%s\",\"sendTime\":%ld,\"status\":1}",
        msg_id, conversation_id, sender_id, content_type, content, (long)send_time);
    if (n < 0 || (size_t)n >= sizeof(doc)) return -1;

    int rc = miku_mongo_insert(store->mongo, "messages", doc);

    if (rc == 0 && out_msg_id && msg_id_cap > 0)
        strncpy(out_msg_id, msg_id, msg_id_cap - 1);
    return rc;
}

int miku_msg_store_find_by_conv(miku_msg_store_t *store, const char *conversation_id,
                                  int64_t start_seq, int64_t end_seq,
                                  char **results_json) {
    if (!store || !conversation_id) return -1;
    if (!store->enabled) {
        if (results_json) *results_json = strdup("[]");
        return 0;
    }

    char filter[256];
    snprintf(filter, sizeof(filter),
             "{\"conversationID\":\"%s\",\"seq\":{\"$gte\":%ld,\"$lte\":%ld}}",
             conversation_id, (long)start_seq, (long)end_seq);

    return miku_mongo_find_one(store->mongo, "messages", filter, results_json);
}

int miku_msg_store_find_one(miku_msg_store_t *store, const char *msg_id,
                              char **result_json) {
    if (!store || !msg_id) return -1;
    if (!store->enabled) {
        if (result_json) *result_json = strdup("{}");
        return 0;
    }

    char filter[128];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
    return miku_mongo_find_one(store->mongo, "messages", filter, result_json);
}

int miku_msg_store_update_status(miku_msg_store_t *store, const char *msg_id, int status) {
    if (!store || !msg_id) return -1;
    if (!store->enabled) return 0;

    char filter[128], update[128];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
    snprintf(update, sizeof(update), "{\"$set\":{\"status\":%d}}", status);
    return miku_mongo_update(store->mongo, "messages", filter, update, false);
}

int miku_msg_store_delete(miku_msg_store_t *store, const char *msg_id) {
    if (!store || !msg_id) return -1;
    if (!store->enabled) return 0;

    char filter[128];
    snprintf(filter, sizeof(filter), "{\"msgID\":\"%s\"}", msg_id);
    return miku_mongo_delete(store->mongo, "messages", filter);
}
