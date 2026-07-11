#include "miku_mt_pipeline.h"
#include "miku_log.h"
#include "miku_seq.h"
#include <stdlib.h>
#include <string.h>

struct miku_mt_pipeline_s {
    miku_msg_t           batch[MK_PIPELINE_BATCH_SIZE];
    int                  batch_count;
    miku_seq_t          *seq;
    miku_mt_to_redis_fn  to_redis;
    void                *to_redis_ctx;
    miku_mt_to_mongo_fn  to_mongo;
    void                *to_mongo_ctx;
    miku_mt_to_push_fn   to_push;
    void                *to_push_ctx;

    struct { char user_id[64]; char conv_id[128]; int64_t seq; } read_seqs[4096];
    int                  read_seq_count;
};

miku_mt_pipeline_t *miku_mt_pipeline_create(void) {
    miku_mt_pipeline_t *p = (miku_mt_pipeline_t *)calloc(1, sizeof(miku_mt_pipeline_t));
    if (!p) return NULL;
    p->seq = miku_seq_create();
    if (!p->seq) { free(p); return NULL; }
    return p;
}

void miku_mt_pipeline_destroy(miku_mt_pipeline_t *p) {
    if (!p) return;
    miku_seq_destroy(p->seq);
    free(p);
}

int miku_mt_pipeline_submit(miku_mt_pipeline_t *p, const miku_msg_t *msg) {
    if (!p || !msg) return -1;
    if (p->batch_count >= MK_PIPELINE_BATCH_SIZE) {
        miku_mt_pipeline_flush(p);
    }
    memcpy(&p->batch[p->batch_count++], msg, sizeof(miku_msg_t));
    return 0;
}

int miku_mt_pipeline_flush(miku_mt_pipeline_t *p) {
    if (!p || p->batch_count == 0) return 0;
    if (p->to_redis) p->to_redis(p->batch, p->batch_count, p->to_redis_ctx);
    if (p->to_mongo) p->to_mongo(p->batch, p->batch_count, p->to_mongo_ctx);
    for (int i = 0; i < p->batch_count; i++) {
        if (p->to_push) {
            p->to_push(p->batch[i].send_id, p->batch[i].recv_id,
                        p->batch[i].seq, p->to_push_ctx);
        }
    }
    MK_LOG_DEBUG("pipeline: flushed %d messages", p->batch_count);
    p->batch_count = 0;
    return 0;
}

int miku_mt_pipeline_pending(miku_mt_pipeline_t *p) {
    return p ? p->batch_count : 0;
}

int64_t miku_mt_pipeline_seq_next(miku_mt_pipeline_t *p, const char *conv_id) {
    if (!p || !p->seq) return -1;
    const char *cid = (conv_id && conv_id[0]) ? conv_id : "default";
    return miku_seq_next(p->seq, cid);
}

void miku_mt_pipeline_on_redis(miku_mt_pipeline_t *p, miku_mt_to_redis_fn fn, void *ctx) {
    if (!p) return;
    p->to_redis = fn; p->to_redis_ctx = ctx;
}

void miku_mt_pipeline_on_mongo(miku_mt_pipeline_t *p, miku_mt_to_mongo_fn fn, void *ctx) {
    if (!p) return;
    p->to_mongo = fn; p->to_mongo_ctx = ctx;
}

void miku_mt_pipeline_on_push(miku_mt_pipeline_t *p, miku_mt_to_push_fn fn, void *ctx) {
    if (!p) return;
    p->to_push = fn; p->to_push_ctx = ctx;
}

int miku_mt_pipeline_process_read_seq(miku_mt_pipeline_t *p, const char *user_id,
                                        const char *conv_id, int64_t has_read_seq) {
    if (!p || !user_id || !conv_id) return -1;
    if (p->read_seq_count >= 4096) return -1;
    strncpy(p->read_seqs[p->read_seq_count].user_id, user_id, 63);
    strncpy(p->read_seqs[p->read_seq_count].conv_id, conv_id, 127);
    p->read_seqs[p->read_seq_count].seq = has_read_seq;
    p->read_seq_count++;
    return 0;
}

int64_t miku_mt_pipeline_get_read_seq(miku_mt_pipeline_t *p, const char *user_id,
                                        const char *conv_id) {
    if (!p || !user_id || !conv_id) return 0;
    for (int i = p->read_seq_count - 1; i >= 0; i--) {
        if (strcmp(p->read_seqs[i].user_id, user_id) == 0 &&
            strcmp(p->read_seqs[i].conv_id, conv_id) == 0)
            return p->read_seqs[i].seq;
    }
    return 0;
}
