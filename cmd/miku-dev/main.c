#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_api.h"
#include "miku_http_server.h"
#include "miku_msggateway.h"
#include "miku_msgtransfer.h"
#include "miku_mt_pipeline.h"
#include "miku_push.h"
#include "miku_offline_push.h"
#include "miku_crontask.h"
#include "miku_cron_tasks.h"
#include "miku_im_message.h"
#include "miku_ws_subscription.h"
#include "miku_auth.h"
#include "miku_user.h"
#include "miku_friend.h"
#include "miku_group.h"
#include "miku_conversation.h"
#include "miku_msg.h"
#include "miku_third.h"
#include "miku_rpc_server.h"
#include "miku_middleware.h"
#include "miku_version.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static miku_graceful_t g_graceful;
static miku_cron_tasks_t *g_cron_impl;

static void dev_cron_delete_msg(void *ctx) {
    miku_cron_delete_expired_msgs((miku_cron_tasks_t *)ctx, 180);
}

static void dev_cron_clear_s3(void *ctx) {
    miku_cron_clear_s3_files((miku_cron_tasks_t *)ctx, 30);
}

static void dev_pipeline_redis(const miku_msg_t *msgs, int count, void *ctx) {
    (void)msgs; (void)ctx;
    MK_LOG_DEBUG("dev pipeline: %d msgs to Redis", count);
}

static void dev_pipeline_mongo(const miku_msg_t *msgs, int count, void *ctx) {
    (void)msgs; (void)ctx;
    MK_LOG_DEBUG("dev pipeline: %d msgs to MongoDB", count);
}

static void dev_pipeline_push(const char *user_id, const char *conv_id, int64_t seq, void *ctx) {
    (void)ctx;
    MK_LOG_DEBUG("dev pipeline: push user=%s conv=%s seq=%ld", user_id, conv_id, (long)seq);
}

static void dev_kick_user(const char *user_id, int platform, void *ctx) {
    (void)platform;
    miku_msggw_t *gw = (miku_msggw_t *)ctx;
    if (gw && user_id) {
        int n = miku_msggw_kick_user(gw, user_id);
        MK_LOG_INFO("force_logout: kicked %d WS session(s) for %s", n, user_id);
    }
}

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    int api_port = -1;
    int ws_port = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) api_port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) ws_port = atoi(argv[++i]);
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);
    miku_service_config_print(&sc);

    if (api_port < 0) api_port = sc.api_port;
    if (ws_port < 0)  ws_port = sc.ws_port;

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 500);

    MK_LOG_INFO("=== miku-dev %s ===", MIKU_VERSION_FULL);

    miku_api_ctx_t *ctx = miku_api_ctx_create();
    if (!ctx) { MK_LOG_ERROR("Failed to create API context"); return 1; }
    ctx->stats.port = api_port;

    miku_http_server_t *srv = miku_http_server_create(sc.listen_ip, api_port);
    if (!srv) { MK_LOG_ERROR("Failed to create HTTP server"); return 1; }

    miku_http_server_set_stats(srv, &ctx->stats);
    miku_http_server_use(srv, miku_mw_cors, NULL);
    miku_http_server_use(srv, miku_mw_request_id, NULL);
    miku_http_server_use(srv, miku_mw_logging, NULL);
    static miku_auth_mw_cfg_t auth_cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_server_use(srv, miku_mw_auth, &auth_cfg);
    miku_http_server_use(srv, miku_mw_stats, &ctx->stats);
    static miku_rate_limit_cfg_t rl = { .window_ms = 60000, .max_requests = 100, .enabled = 1 };
    miku_http_server_use(srv, miku_mw_rate_limit, &rl);
    miku_api_register_routes(srv, ctx);

    miku_msggw_t *gw = miku_msggw_create(ws_port);
    ctx->on_kick = dev_kick_user;
    ctx->on_kick_ctx = gw;
    miku_ws_sub_t *sub = miku_ws_sub_create();
    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    miku_mt_pipeline_t *pipe = miku_mt_pipeline_create();
    miku_mt_pipeline_on_redis(pipe, dev_pipeline_redis, NULL);
    miku_mt_pipeline_on_mongo(pipe, dev_pipeline_mongo, NULL);
    miku_mt_pipeline_on_push(pipe, dev_pipeline_push, NULL);

    miku_push_t *push = miku_push_create();
    miku_offline_push_t *offline = miku_offline_push_create(MK_PUSH_PROVIDER_DUMMY);

    miku_crontask_t *cron = miku_crontask_create();
    g_cron_impl = miku_cron_tasks_create();
    miku_crontask_add(cron, "deleteMsg", dev_cron_delete_msg, g_cron_impl, 86400000);
    miku_crontask_add(cron, "clearS3",   dev_cron_clear_s3,   g_cron_impl, 604800000);

    miku_msggw_start(gw);
    miku_msgtransfer_start(mt);
    miku_push_start(push);
    miku_crontask_start(cron);

    MK_LOG_INFO("=== miku-dev ready ===");
    MK_LOG_INFO("  API:        http://%s:%d", sc.listen_ip, api_port);
    MK_LOG_INFO("  WebSocket:  ws://%s:%d", sc.listen_ip, ws_port);
    MK_LOG_INFO("  Routes:     203");
    MK_LOG_INFO("  Mongo:      %s/%s", sc.mongo_uri, sc.mongo_database);
    MK_LOG_INFO("  Redis:      %s", sc.redis_address);
    MK_LOG_INFO("  Kafka:      %s", sc.kafka_brokers);
    MK_LOG_INFO("  Services:   7 RPC + 5 gateway");
    MK_LOG_INFO("  Push:       online + offline (%s)", miku_offline_push_provider_name(MK_PUSH_PROVIDER_DUMMY));
    MK_LOG_INFO("  Cron:       %d tasks", miku_crontask_task_count(cron));
    MK_LOG_INFO("  Press Ctrl+C to stop");

    int tick = 0;
    while (miku_graceful_running(&g_graceful)) {
        miku_crontask_tick(cron);
        miku_msggw_poll(gw, 10);

        miku_msg_t msg;
        while (miku_msgtransfer_dequeue(mt, &msg) == 0) {
            msg.seq = miku_mt_pipeline_seq_next(pipe, msg.recv_id);
            miku_mt_pipeline_submit(pipe, &msg);
        }
        tick++;
        if (tick % 10 == 0) miku_mt_pipeline_flush(pipe);

        usleep(50000);
    }

    MK_LOG_INFO("=== miku-dev shutting down ===");
    miku_mt_pipeline_flush(pipe);
    miku_crontask_stop(cron);
    miku_push_stop(push);
    miku_msgtransfer_stop(mt);
    miku_msggw_stop(gw);
    miku_http_server_stop(srv);
    miku_http_server_destroy(srv);

    miku_crontask_destroy(cron);
    miku_cron_tasks_destroy(g_cron_impl);
    miku_push_destroy(push);
    miku_offline_push_destroy(offline);
    miku_msgtransfer_destroy(mt);
    miku_mt_pipeline_destroy(pipe);
    miku_msggw_destroy(gw);
    miku_ws_sub_destroy(sub);
    miku_api_ctx_destroy(ctx);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
