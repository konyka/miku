#ifndef MIKU_MSG_H
#define MIKU_MSG_H

#include "miku_common.h"
#include "miku_models.h"
#include "miku_json.h"

#define MK_MAX_MSGS 65536

typedef struct miku_msg_service_s miku_msg_service_t;
typedef struct miku_group_service_s miku_group_service_t;

MIKU_API miku_msg_service_t *miku_msg_service_create(void);
MIKU_API void miku_msg_service_destroy(miku_msg_service_t *svc);
/* Optional group service for getMsg membership checks (non-owning). */
MIKU_API void miku_msg_service_set_group_svc(miku_msg_service_t *svc,
                                              miku_group_service_t *group);

MIKU_API int miku_msg_send(miku_msg_service_t *svc, miku_msg_t *m);
MIKU_API int miku_msg_get_by_conv(miku_msg_service_t *svc, const char *conv_id,
                                   int64_t start, int64_t end, int count,
                                   miku_msg_t *out, int max);
MIKU_API int miku_msg_revoke(miku_msg_service_t *svc, const char *user_id, const char *client_msg_id);
/* Align API msg-service record with gateway-authoritative seq/id/time. */
MIKU_API int miku_msg_update_delivery(miku_msg_service_t *svc, const char *client_msg_id,
                                      int64_t seq, const char *server_msg_id, int64_t send_time);

MIKU_API void miku_msg_handle_rpc(miku_msg_service_t *svc, const char *method,
                                   const miku_json_val_t *req, miku_json_val_t *resp);
#endif
