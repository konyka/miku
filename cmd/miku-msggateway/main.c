#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msggateway.h"
#include "miku_im_message.h"
#include "miku_ws_subscription.h"
#include "miku_msggw_ws_ops.h"
#include "miku_msg_store.h"
#include "miku_group.h"
#include "miku_json.h"
#include "miku_http_server.h"
#include "miku_token.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

static miku_graceful_t g_graceful;
static miku_http_server_t *g_admin;
static miku_msggw_t *g_gw;
static miku_msg_store_t *g_store;
static miku_group_service_t *g_group;

static void handle_internal_kick(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_msggw_t *gw = (miku_msggw_t *)ctx;
    char uid[64] = {0};
    int platform = -1;
    if (req && req->body.data && req->body.len > 0) {
        char *tmp = strndup(req->body.data, req->body.len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            const char *u = miku_json_str(miku_json_get(j, "userID"));
            if (u) strncpy(uid, u, sizeof(uid) - 1);
            miku_json_val_t *p = miku_json_get(j, "platformID");
            if (p) platform = (int)miku_json_int(p);
            miku_json_destroy(j);
        }
    }
    int kicked = 0;
    if (uid[0]) {
        /* Propagate revoke into this process so WS re-handshake rejects the token. */
        miku_token_revoke(uid, platform);
        if (gw)
            kicked = miku_msggw_kick_user(gw, uid, platform);
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"errCode\":0,\"errMsg\":\"\",\"kicked\":%d}", kicked);
    miku_http_response_set_json(resp, buf);
    MK_LOG_INFO("internal kick+revoke user=%s platform=%d kicked=%d",
                uid[0] ? uid : "(empty)", platform, kicked);
}

static void handle_internal_group_member(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_group_service_t *group = (miku_group_service_t *)ctx;
    char gid[64] = {0}, uid[64] = {0};
    int role = 20;
    if (req && req->body.data && req->body.len > 0) {
        char *tmp = strndup(req->body.data, req->body.len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            const char *g = miku_json_str(miku_json_get(j, "groupID"));
            const char *u = miku_json_str(miku_json_get(j, "userID"));
            if (g) strncpy(gid, g, sizeof(gid) - 1);
            if (u) strncpy(uid, u, sizeof(uid) - 1);
            int64_t r = miku_json_int(miku_json_get(j, "role"));
            if (r != 0) role = (int)r;
            miku_json_destroy(j);
        }
    }
    int rc = -1;
    if (group && gid[0] && uid[0])
        rc = miku_group_add_member(group, gid, uid, role);
    char buf[128];
    snprintf(buf, sizeof(buf), "{\"errCode\":%d,\"errMsg\":\"\"}", rc == 0 ? 0 : 500);
    miku_http_response_set_json(resp, buf);
    MK_LOG_INFO("internal group_member group=%s user=%s rc=%d",
                gid[0] ? gid : "(empty)", uid[0] ? uid : "(empty)", rc);
}

static void handle_internal_push_msg(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_msggw_ws_ctx_t *ws = (miku_msggw_ws_ctx_t *)ctx;
    miku_im_msg_t im;
    miku_im_msg_init(&im);
    int rc = -1;
    if (req && req->body.data && req->body.len > 0 && ws) {
        char *tmp = strndup(req->body.data, req->body.len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        if (j) {
            miku_im_msg_from_json(&im, j);
            if (im.content_type <= 0) {
                int64_t mt = miku_json_int(miku_json_get(j, "msgType"));
                im.content_type = mt > 0 ? (int)mt : MK_IM_MSG_TYPE_TEXT;
            }
            miku_json_destroy(j);
            rc = miku_msggw_ws_deliver_msg(ws, &im);
        }
    }
    char buf[256];
    if (rc == 0) {
        snprintf(buf, sizeof(buf),
                 "{\"errCode\":0,\"serverMsgID\":\"%s\",\"seq\":%lld,\"sendTime\":%lld}",
                 im.msg_id, (long long)im.seq, (long long)im.send_time);
    } else {
        snprintf(buf, sizeof(buf), "{\"errCode\":500,\"errMsg\":\"deliver failed\"}");
    }
    miku_http_response_set_json(resp, buf);
    MK_LOG_INFO("internal push_msg send=%s recv=%s group=%s rc=%d seq=%lld",
                im.send_id, im.recv_id, im.group_id, rc, (long long)im.seq);
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

    g_store = miku_msg_store_create(NULL);
    if (!g_store) {
        MK_LOG_ERROR("Failed to create msg_store");
        miku_msggw_destroy(g_gw);
        return 1;
    }

    g_group = miku_group_service_create();
    if (!g_group) {
        MK_LOG_ERROR("Failed to create group service");
        miku_msg_store_destroy(g_store);
        miku_msggw_destroy(g_gw);
        return 1;
    }

    miku_ws_sub_t *sub = miku_ws_sub_create();
    if (!sub) {
        MK_LOG_ERROR("Failed to create subscription manager");
        miku_group_service_destroy(g_group);
        miku_msg_store_destroy(g_store);
        miku_msggw_destroy(g_gw);
        return 1;
    }

    static miku_msggw_ws_ctx_t gctx;
    gctx.gw = g_gw;
    gctx.sub = sub;
    gctx.store = g_store;
    gctx.group = g_group;

    miku_ws_sub_set_notify(sub, miku_msggw_ws_sub_notify, g_gw);
    miku_msggw_on_message(g_gw, on_ws_message, &gctx);
    miku_msggw_on_opcode(g_gw, miku_msggw_ws_on_opcode, &gctx);
    miku_msggw_on_presence(g_gw, miku_msggw_ws_on_presence, &gctx);

    g_admin = miku_http_server_create("127.0.0.1", admin_port);
    if (g_admin) {
        miku_http_server_route(g_admin, "POST", "/internal/kick", handle_internal_kick, g_gw);
        miku_http_server_route(g_admin, "POST", "/internal/group_member",
                               handle_internal_group_member, g_group);
        miku_http_server_route(g_admin, "POST", "/internal/push_msg",
                               handle_internal_push_msg, &gctx);
        pthread_t th;
        if (pthread_create(&th, NULL, admin_thread, NULL) == 0)
            pthread_detach(th);
        else
            MK_LOG_WARN("failed to start admin HTTP thread");
    } else {
        MK_LOG_WARN("admin HTTP on :%d unavailable", admin_port);
    }

    miku_msggw_start(g_gw);
    MK_LOG_INFO("miku-msggateway ready (ws://0.0.0.0:%d, group fan-out wired)", port);

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
    miku_group_service_destroy(g_group);
    miku_msg_store_destroy(g_store);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
