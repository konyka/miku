#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msggateway.h"
#include "miku_im_message.h"
#include "miku_ws_subscription.h"
#include "miku_json.h"
#include "miku_http_server.h"
#include "miku_http.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static miku_graceful_t g_graceful;
static miku_http_server_t *g_admin;
static miku_msggw_t *g_gw;

typedef struct {
    miku_msggw_t    *gw;
    miku_ws_sub_t   *sub;
} gw_ctx_t;

static void handle_internal_kick(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_msggw_t *gw = (miku_msggw_t *)ctx;
    char uid[64] = {0};
    if (req && req->body.data && req->body.len > 0) {
        char *tmp = strndup(req->body.data, req->body.len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            const char *u = miku_json_str(miku_json_get(j, "userID"));
            if (u) strncpy(uid, u, sizeof(uid) - 1);
            miku_json_destroy(j);
        }
    }
    int kicked = 0;
    if (uid[0] && gw)
        kicked = miku_msggw_kick_user(gw, uid);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"errCode\":0,\"errMsg\":\"\",\"kicked\":%d}", kicked);
    miku_http_response_set_json(resp, buf);
    MK_LOG_INFO("internal kick user=%s kicked=%d", uid[0] ? uid : "(empty)", kicked);
}

static void *admin_thread(void *arg) {
    (void)arg;
    if (g_admin) miku_http_server_start(g_admin);
    return NULL;
}

static void on_ws_message(const char *user_id, const char *msg, size_t len, void *ctx) {
    (void)ctx;
    if (!user_id || !msg) return;
    char *tmp = strndup(msg, len);
    miku_json_val_t *j = miku_json_parse_str(tmp);
    free(tmp);
    if (!j) {
        MK_LOG_DEBUG("ws_msg: user=%s (non-json, len=%zu)", user_id, len);
        return;
    }
    miku_im_msg_t im;
    if (miku_im_msg_from_json(&im, j) == 0) {
        MK_LOG_INFO("ws_msg: user=%s msgType=%d convType=%d sendID=%s convID=%s",
                     user_id, im.content_type, im.conversation_type,
                     im.send_id, im.conversation_id);
    }
    miku_json_destroy(j);
}

static void on_ws_opcode(int client_idx, int opcode, const char *payload, size_t len, void *ctx) {
    gw_ctx_t *gc = (gw_ctx_t *)ctx;
    if (!gc) return;

    switch (opcode) {
    case MK_WS_OP_GET_NEWEST_SEQ:
        MK_LOG_INFO("ws_op[%d]: GET_NEWEST_SEQ client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_PULL_MSG_BY_SEQ:
        MK_LOG_INFO("ws_op[%d]: PULL_MSG_BY_SEQ client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_SEND_MSG: {
        char *tmp = strndup(payload, len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            miku_im_msg_t im;
            if (miku_im_msg_from_json(&im, j) == 0) {
                MK_LOG_INFO("ws_op[%d]: SEND_MSG client=%d sendID=%s recvID=%s type=%d",
                             opcode, client_idx, im.send_id, im.recv_id, im.content_type);
            }
            miku_json_destroy(j);
        }
        break;
    }
    case MK_WS_OP_SEND_SIGNAL_MSG:
        MK_LOG_INFO("ws_op[%d]: SEND_SIGNAL_MSG client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_PULL_MSG:
        MK_LOG_INFO("ws_op[%d]: PULL_MSG client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_GET_CONV_MAX_READ_SEQ:
        MK_LOG_INFO("ws_op[%d]: GET_CONV_MAX_READ_SEQ client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_PULL_CONV_LAST_MSG:
        MK_LOG_INFO("ws_op[%d]: PULL_CONV_LAST_MSG client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_PUSH_MSG:
        MK_LOG_INFO("ws_op[%d]: PUSH_MSG client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_KICK_ONLINE:
        MK_LOG_INFO("ws_op[%d]: KICK_ONLINE client=%d", opcode, client_idx);
        break;
    case MK_WS_OP_LOGOUT:
        MK_LOG_INFO("ws_op[%d]: LOGOUT client=%d — disconnecting", opcode, client_idx);
        miku_msggw_disconnect_client(gc->gw, client_idx);
        break;
    case MK_WS_OP_SET_BACKGROUND: {
        int bg = 0;
        if (payload && len > 0) {
            char *tmp = strndup(payload, len);
            miku_json_val_t *j = miku_json_parse_str(tmp);
            free(tmp);
            if (j) {
                bg = (int)miku_json_int(miku_json_get(j, "isBackground"));
                miku_json_destroy(j);
            }
        }
        miku_msggw_set_background(gc->gw, client_idx, bg != 0);
        MK_LOG_INFO("ws_op[%d]: SET_BACKGROUND client=%d bg=%d", opcode, client_idx, bg);
        break;
    }
    case MK_WS_OP_SUB_USER_STATUS: {
        char *tmp = strndup(payload, len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            const char *target = miku_json_str(miku_json_get(j, "userID"));
            const char *action = miku_json_str(miku_json_get(j, "action"));
            if (target && action) {
                if (strcmp(action, "subscribe") == 0) {
                    miku_ws_sub_subscribe(gc->sub, "self", target);
                    MK_LOG_INFO("ws_op[%d]: SUB_USER_STATUS subscribe target=%s", opcode, target);
                } else if (strcmp(action, "unsubscribe") == 0) {
                    miku_ws_sub_unsubscribe(gc->sub, "self", target);
                    MK_LOG_INFO("ws_op[%d]: SUB_USER_STATUS unsubscribe target=%s", opcode, target);
                }
            }
            miku_json_destroy(j);
        }
        break;
    }
    case MK_WS_OP_DATA_ERROR:
        MK_LOG_WARN("ws_op[%d]: DATA_ERROR client=%d", opcode, client_idx);
        break;
    default:
        MK_LOG_DEBUG("ws_op: unknown opcode=%d client=%d", opcode, client_idx);
        break;
    }
}

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    int port = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);
    if (port < 0) port = sc.ws_port;
    int admin_port = port + 1;

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 500);
    MK_LOG_INFO("miku-msggateway starting on :%d (admin :%d)", port, admin_port);

    g_gw = miku_msggw_create(port);
    if (!g_gw) { MK_LOG_ERROR("Failed to create message gateway"); return 1; }

    miku_ws_sub_t *sub = miku_ws_sub_create();
    if (!sub) { MK_LOG_ERROR("Failed to create subscription manager"); miku_msggw_destroy(g_gw); return 1; }

    static gw_ctx_t gctx;
    gctx.gw = g_gw;
    gctx.sub = sub;

    miku_msggw_on_message(g_gw, on_ws_message, &gctx);
    miku_msggw_on_opcode(g_gw, on_ws_opcode, &gctx);

    g_admin = miku_http_server_create("127.0.0.1", admin_port);
    if (g_admin) {
        miku_http_server_route(g_admin, "POST", "/internal/kick", handle_internal_kick, g_gw);
        pthread_t th;
        if (pthread_create(&th, NULL, admin_thread, NULL) == 0)
            pthread_detach(th);
        else
            MK_LOG_WARN("failed to start admin HTTP thread");
    } else {
        MK_LOG_WARN("admin HTTP on :%d unavailable", admin_port);
    }

    miku_msggw_start(g_gw);
    MK_LOG_INFO("miku-msggateway ready (ws://0.0.0.0:%d, admin POST /internal/kick)", port);

    while (miku_graceful_running(&g_graceful)) {
        miku_msggw_poll(g_gw, 100);
    }

    MK_LOG_INFO("miku-msggateway shutting down");
    if (g_admin) {
        miku_http_server_stop(g_admin);
        miku_http_server_destroy(g_admin);
        g_admin = NULL;
    }
    miku_msggw_stop(g_gw);
    miku_msggw_destroy(g_gw);
    miku_ws_sub_destroy(sub);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
