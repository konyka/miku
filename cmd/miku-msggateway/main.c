#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msggateway.h"
#include "miku_im_message.h"
#include "miku_ws_subscription.h"
#include "miku_json.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;

typedef struct {
    miku_msggw_t    *gw;
    miku_ws_sub_t   *sub;
} gw_ctx_t;

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

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 500);
    MK_LOG_INFO("miku-msggateway starting on :%d", port);

    miku_msggw_t *gw = miku_msggw_create(port);
    if (!gw) { MK_LOG_ERROR("Failed to create message gateway"); return 1; }

    miku_ws_sub_t *sub = miku_ws_sub_create();
    if (!sub) { MK_LOG_ERROR("Failed to create subscription manager"); miku_msggw_destroy(gw); return 1; }

    static gw_ctx_t gctx;
    gctx.gw = gw;
    gctx.sub = sub;

    miku_msggw_on_message(gw, on_ws_message, &gctx);
    miku_msggw_on_opcode(gw, on_ws_opcode, &gctx);

    miku_msggw_start(gw);
    MK_LOG_INFO("miku-msggateway ready (ws://0.0.0.0:%d) — 12 opcodes wired", port);

    while (miku_graceful_running(&g_graceful)) {
        miku_msggw_poll(gw, 100);
    }

    MK_LOG_INFO("miku-msggateway shutting down");
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
    miku_ws_sub_destroy(sub);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
