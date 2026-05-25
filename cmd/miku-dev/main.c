#include "miku_common.h"
#include "miku_log.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

static volatile int g_running = 1;
static void signal_handler(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    int api_port = 10002;
    int ws_port = 10001;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) api_port = atoi(argv[++i]);
        if (strcmp(argv[i], "-w") == 0 && i + 1 < argc) ws_port = atoi(argv[++i]);
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    miku_log_init(NULL, MK_LOG_DEBUG);

    MK_LOG_INFO("=== miku-dev: starting all services ===");

    miku_api_ctx_t *ctx = miku_api_ctx_create();
    if (!ctx) { MK_LOG_ERROR("Failed to create API context"); return 1; }

    miku_http_server_t *srv = miku_http_server_create("0.0.0.0", api_port);
    if (!srv) { MK_LOG_ERROR("Failed to create HTTP server"); return 1; }

    miku_http_server_use(srv, miku_mw_cors, NULL);
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
    MK_LOG_INFO("  API:        http://0.0.0.0:%d", api_port);
    MK_LOG_INFO("  WebSocket:  ws://0.0.0.0:%d", ws_port);
    MK_LOG_INFO("  Services:   7 RPC + 5 gateway");
    MK_LOG_INFO("  Press Ctrl+C to stop");

    while (g_running) {
        miku_crontask_tick(cron);
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
    return 0;
}
