#ifndef MIKU_MSG_STORE_H
#define MIKU_MSG_STORE_H

#include "miku_common.h"
#include "miku_mongo.h"

typedef struct miku_msg_store_s miku_msg_store_t;

/* Pass-28 T0-P3: callback fired when the in-memory ring overwrites the
 * oldest message (data-loss with errCode:0). Subscribers (push pipeline,
 * monitoring) use this to emit a load-shed signal. */
typedef void (*miku_msg_store_overwrite_cb)(int evicted_slot, int total_overwrites,
                                           void *ctx);

MIKU_API miku_msg_store_t *miku_msg_store_create(miku_mongo_t *mongo);
MIKU_API void               miku_msg_store_destroy(miku_msg_store_t *store);

/* Configure an overwrite callback. Pass NULL to clear. */
MIKU_API void  miku_msg_store_set_overwrite_cb(miku_msg_store_t *store,
                                                miku_msg_store_overwrite_cb cb,
                                                void *ctx);

MIKU_API int  miku_msg_store_insert(miku_msg_store_t *store, const char *conversation_id,
                                     const char *sender_id, int content_type,
                                     const char *content, int64_t send_time, int64_t seq,
                                     char *out_msg_id, size_t msg_id_cap);
MIKU_API int  miku_msg_store_find_by_conv(miku_msg_store_t *store, const char *conversation_id,
                                           int64_t start_seq, int64_t end_seq,
                                           char **results_json);
MIKU_API int  miku_msg_store_find_one(miku_msg_store_t *store, const char *msg_id,
                                        char **result_json);
MIKU_API int  miku_msg_store_update_status(miku_msg_store_t *store, const char *msg_id,
                                             int status);
MIKU_API int  miku_msg_store_delete(miku_msg_store_t *store, const char *msg_id);

/* Delete messages with send_time < cutoff_ms. Returns number removed. */
MIKU_API int  miku_msg_store_purge_older_than(miku_msg_store_t *store, int64_t cutoff_ms);
/* Delete all messages from a sender. Returns number removed. */
MIKU_API int  miku_msg_store_clear_user(miku_msg_store_t *store, const char *user_id);
MIKU_API int  miku_msg_store_count(miku_msg_store_t *store);

#endif
