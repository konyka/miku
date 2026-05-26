#ifndef MIKU_API_H
#define MIKU_API_H

#include "miku_common.h"
#include "miku_http_server.h"
#include "miku_auth.h"
#include "miku_user.h"
#include "miku_friend.h"
#include "miku_group.h"
#include "miku_conversation.h"
#include "miku_msg.h"
#include "miku_third.h"
#include "miku_stats.h"
#include "miku_ratelimit.h"
#include "miku_webhook.h"

typedef struct {
    miku_auth_service_t       *auth;
    miku_user_service_t       *user;
    miku_friend_service_t     *friend_svc;
    miku_group_service_t      *group_svc;
    miku_conv_service_t       *conv;
    miku_msg_service_t        *msg;
    miku_third_service_t      *third;
    miku_stats_t              stats;
    miku_ratelimit_t          *ratelimit;
    miku_webhook_t            *webhook;
} miku_api_ctx_t;

MIKU_API miku_api_ctx_t *miku_api_ctx_create(void);
MIKU_API void miku_api_ctx_destroy(miku_api_ctx_t *ctx);

MIKU_API int miku_api_register_routes(miku_http_server_t *srv, miku_api_ctx_t *ctx);

#endif
