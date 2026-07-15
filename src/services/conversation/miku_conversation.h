#ifndef MIKU_CONVERSATION_H
#define MIKU_CONVERSATION_H

#include "miku_common.h"
#include "miku_models.h"
#include "miku_json.h"

#define MK_MAX_CONVS 8192

typedef struct miku_conv_service_s miku_conv_service_t;

MIKU_API miku_conv_service_t *miku_conv_service_create(void);
MIKU_API void miku_conv_service_destroy(miku_conv_service_t *svc);

MIKU_API int miku_conv_create(miku_conv_service_t *svc, const miku_conversation_t *c);
MIKU_API int miku_conv_get(miku_conv_service_t *svc, const char *owner, const char *conv_id, miku_conversation_t *out);
MIKU_API int miku_conv_get_all(miku_conv_service_t *svc, const char *owner, miku_conversation_t *out, int max);
MIKU_API int miku_conv_update(miku_conv_service_t *svc, const miku_conversation_t *c);

/* Upsert conversation metadata after a send; bump_unread increments unread_count. */
MIKU_API void miku_conv_touch_on_send(miku_conv_service_t *svc, const char *owner,
                                      const char *cid, int conv_type,
                                      const char *peer_user_id, const char *group_id,
                                      int64_t send_time, const char *content,
                                      int bump_unread);

MIKU_API void miku_conv_handle_rpc(miku_conv_service_t *svc, const char *method,
                                    const miku_json_val_t *req, miku_json_val_t *resp);
#endif
