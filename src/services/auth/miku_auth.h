#ifndef MIKU_AUTH_H
#define MIKU_AUTH_H

#include "miku_common.h"
#include "miku_rpc.h"
#include "miku_models.h"

typedef struct miku_auth_service_s miku_auth_service_t;

MIKU_API miku_auth_service_t *miku_auth_service_create(void);
MIKU_API void miku_auth_service_destroy(miku_auth_service_t *svc);

MIKU_API int miku_auth_user_token(miku_auth_service_t *svc, const char *user_id,
                                   const char *secret, int platform,
                                   char *token_out, size_t token_cap);
MIKU_API int miku_auth_parse_token(miku_auth_service_t *svc, const char *token,
                                    char *user_id_out, size_t cap);
MIKU_API int miku_auth_force_logout(miku_auth_service_t *svc, const char *user_id, int platform);

MIKU_API void miku_auth_handle_rpc(miku_auth_service_t *svc, const miku_rpc_message_t *req,
                                    miku_rpc_message_t *resp);

#endif
