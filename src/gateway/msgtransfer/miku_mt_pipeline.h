#ifndef MIKU_MSGTRANSFER_PIPELINE_H
#define MIKU_MSGTRANSFER_PIPELINE_H

#include "miku_common.h"
#include "miku_models.h"

#define MK_PIPELINE_BATCH_SIZE 100

typedef struct miku_mt_pipeline_s miku_mt_pipeline_t;

typedef void (*miku_mt_to_redis_fn)(const miku_msg_t *msgs, int count, void *ctx);
typedef void (*miku_mt_to_mongo_fn)(const miku_msg_t *msgs, int count, void *ctx);
typedef void (*miku_mt_to_push_fn)(const char *user_id, const char *conv_id, int64_t seq, void *ctx);

MIKU_API miku_mt_pipeline_t *miku_mt_pipeline_create(void);
MIKU_API void                miku_mt_pipeline_destroy(miku_mt_pipeline_t *p);

MIKU_API int   miku_mt_pipeline_submit(miku_mt_pipeline_t *p, const miku_msg_t *msg);
MIKU_API int   miku_mt_pipeline_flush(miku_mt_pipeline_t *p);
MIKU_API int   miku_mt_pipeline_pending(miku_mt_pipeline_t *p);
MIKU_API int64_t miku_mt_pipeline_seq_next(miku_mt_pipeline_t *p, const char *conv_id);

MIKU_API void  miku_mt_pipeline_on_redis(miku_mt_pipeline_t *p, miku_mt_to_redis_fn fn, void *ctx);
MIKU_API void  miku_mt_pipeline_on_mongo(miku_mt_pipeline_t *p, miku_mt_to_mongo_fn fn, void *ctx);
MIKU_API void  miku_mt_pipeline_on_push(miku_mt_pipeline_t *p, miku_mt_to_push_fn fn, void *ctx);

MIKU_API int   miku_mt_pipeline_process_read_seq(miku_mt_pipeline_t *p, const char *user_id,
                                                   const char *conv_id, int64_t has_read_seq);
MIKU_API int64_t miku_mt_pipeline_get_read_seq(miku_mt_pipeline_t *p, const char *user_id,
                                                 const char *conv_id);

#endif
