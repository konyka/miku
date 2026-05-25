#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_api.h"
#include "miku_http_server.h"
#include "miku_msggateway.h"
#include "miku_msgtransfer.h"
#include "miku_push.h"
#include "miku_crontask.h"
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
    miku_http_server_use(srv, miku_mw_stats, &ctx->stats);
    miku_rate_limit_cfg_t rl = {.window_ms = 1000, .max_requests = 100, .enabled = 1};
    miku_http_server_use(srv, miku_mw_rate_limit, &rl);
    miku_api_register_routes(srv, ctx);
    MK_LOG_INFO("API: %d routes registered", 48);

    miku_msggw_t *gw = miku_msggw_create(ws_port);
    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    miku_push_t *push = miku_push_create();
    miku_crontask_t *cron = miku_crontask_create();

    miku_msggw_start(gw);
    miku_msgtransfer_start(mt);
    miku_push_start(push);
    miku_crontask_start(cron);

    MK_LOG_INFO("=== miku-dev ready ===");
    MK_LOG_INFO("  API:        http://%s:%d", sc.listen_ip, api_port);
    MK_LOG_INFO("  WebSocket:  ws://%s:%d", sc.listen_ip, ws_port);
    MK_LOG_INFO("  Mongo:      %s/%s", sc.mongo_uri, sc.mongo_database);
    MK_LOG_INFO("  Redis:      %s", sc.redis_address);
    MK_LOG_INFO("  Kafka:      %s", sc.kafka_brokers);
    MK_LOG_INFO("  Services:   7 RPC + 5 gateway");
    MK_LOG_INFO("  Press Ctrl+C to stop");

    while (miku_graceful_running(&g_graceful)) {
        miku_crontask_tick(cron);
        miku_msggw_poll(gw, 10);
        usleep(50000);
    }

    MK_LOG_INFO("=== miku-dev shutting down ===");
    miku_crontask_stop(cron);
    miku_push_stop(push);
    miku_msgtransfer_stop(mt);
    miku_msggw_stop(gw);
    miku_http_server_stop(srv);
    miku_http_server_destroy(srv);

    miku_crontask_destroy(cron);
    miku_push_destroy(push);
    miku_msgtransfer_destroy(mt);
    miku_msggw_destroy(gw);
    miku_api_ctx_destroy(ctx);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
