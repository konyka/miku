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
#include "miku_im_message.h"

typedef void (*miku_api_kick_fn)(const char *user_id, int platform, void *ctx);
/* Return 0 on success. May update msg (seq, msg_id, send_time). */
typedef int (*miku_api_msg_sent_fn)(miku_im_msg_t *msg, void *ctx);
/* Sync a group member into msggateway (split deploy). */
typedef void (*miku_api_group_member_fn)(const char *group_id, const char *user_id,
                                          int role, void *ctx);

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
    /* Optional: wired by miku-dev to kick WS sessions on force_logout */
    miku_api_kick_fn           on_kick;
    void                      *on_kick_ctx;
    /* Optional: bridge HTTP sendMsg → WS msg_store + PUSH_MSG */
    miku_api_msg_sent_fn       on_msg_sent;
    void                      *on_msg_sent_ctx;
    /* Optional: sync members to msggateway for group PUSH fan-out */
    miku_api_group_member_fn   on_group_member;
    void                      *on_group_member_ctx;
} miku_api_ctx_t;

MIKU_API miku_api_ctx_t *miku_api_ctx_create(void);
MIKU_API void miku_api_ctx_destroy(miku_api_ctx_t *ctx);

MIKU_API int miku_api_register_routes(miku_http_server_t *srv, miku_api_ctx_t *ctx);

#endif
