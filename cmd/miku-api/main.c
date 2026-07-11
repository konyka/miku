#include "miku_common.h"
#include "miku_log.h"
#include "miku_config.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_http_server.h"
#include "miku_api.h"
#include "miku_middleware.h"
#include "miku_http_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;
static char g_kick_url[128];

static void api_kick_user(const char *user_id, int platform, void *ctx) {
    (void)ctx;
    (void)platform;
    if (!user_id || !g_kick_url[0]) return;
    char body[160];
    snprintf(body, sizeof(body), "{\"userID\":\"%s\"}", user_id);
    int rc = miku_http_post_json(g_kick_url, body);
    if (rc == 0)
        MK_LOG_INFO("force_logout: kicked via %s user=%s", g_kick_url, user_id);
    else
        MK_LOG_WARN("force_logout: kick POST failed (%s) user=%s", g_kick_url, user_id);
}

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    const char *listen_addr = "0.0.0.0";
    int port = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
        else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) listen_addr = argv[++i];
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);
    miku_service_config_print(&sc);

    if (port < 0) port = sc.api_port;
    snprintf(g_kick_url, sizeof(g_kick_url), "http://127.0.0.1:%d/internal/kick", sc.ws_port + 1);

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 500);
    MK_LOG_INFO("miku-api starting on %s:%d (kick→%s)", listen_addr, port, g_kick_url);

    miku_api_ctx_t *ctx = miku_api_ctx_create();
    if (!ctx) { MK_LOG_ERROR("Failed to create API context"); return 1; }
    ctx->stats.port = port;
    ctx->on_kick = api_kick_user;
    ctx->on_kick_ctx = NULL;

    miku_http_server_t *srv = miku_http_server_create(listen_addr, port);
    if (!srv) { MK_LOG_ERROR("Failed to create HTTP server on %s:%d", listen_addr, port); miku_api_ctx_destroy(ctx); return 1; }

    miku_http_server_set_stats(srv, &ctx->stats);
    miku_http_server_use(srv, miku_mw_cors, NULL);
    miku_http_server_use(srv, miku_mw_request_id, NULL);
    miku_http_server_use(srv, miku_mw_logging, NULL);
    static miku_auth_mw_cfg_t auth_cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_server_use(srv, miku_mw_auth, &auth_cfg);
    miku_http_server_use(srv, miku_mw_stats, &ctx->stats);
    miku_api_register_routes(srv, ctx);

    static miku_rate_limit_cfg_t rl_cfg = { .window_ms = 60000, .max_requests = 100, .enabled = 1 };
    miku_http_server_use(srv, miku_mw_rate_limit, &rl_cfg);

    miku_webhook_add_url(ctx->webhook, "http://localhost:8080/webhook");
    miku_webhook_fire(ctx->webhook, MK_WH_USER_ONLINE,
                       "{\"userID\":\"system\",\"event\":\"api_startup\"}");

    MK_LOG_INFO("Registered 203 API routes");

    if (miku_http_server_start(srv) != 0) {
        MK_LOG_ERROR("Failed to start HTTP server");
        miku_http_server_destroy(srv);
        miku_api_ctx_destroy(ctx);
        return 1;
    }

    MK_LOG_INFO("miku-api ready on %s:%d", listen_addr, port);
    miku_graceful_wait(&g_graceful, NULL, NULL);

    MK_LOG_INFO("miku-api shutting down");
    miku_http_server_stop(srv);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
