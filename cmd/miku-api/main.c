#include "miku_common.h"
#include "miku_log.h"
#include "miku_config.h"
#include "miku_http_server.h"
#include "miku_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_running = 1;

static void signal_handler(int sig) {
    (void)sig;
    g_running = 0;
}

int main(int argc, char **argv) {
    const char *config_path = "config/";
    const char *listen_addr = "0.0.0.0";
    int port = 10002;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            listen_addr = argv[++i];
        }
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-api starting (config: %s, listen: %s:%d)", config_path, listen_addr, port);

    miku_api_ctx_t *ctx = miku_api_ctx_create();
    if (!ctx) {
        MK_LOG_ERROR("Failed to create API context");
        return 1;
    }

    miku_http_server_t *srv = miku_http_server_create(listen_addr, port);
    if (!srv) {
        MK_LOG_ERROR("Failed to create HTTP server on %s:%d", listen_addr, port);
        miku_api_ctx_destroy(ctx);
        return 1;
    }

    miku_api_register_routes(srv, ctx);
    MK_LOG_INFO("Registered 21 API routes");

    if (miku_http_server_start(srv) != 0) {
        MK_LOG_ERROR("Failed to start HTTP server");
        miku_http_server_destroy(srv);
        miku_api_ctx_destroy(ctx);
        return 1;
    }

    MK_LOG_INFO("miku-api ready on %s:%d", listen_addr, port);

    while (g_running) {
        usleep(100000);
    }

    MK_LOG_INFO("miku-api shutting down");
    miku_http_server_stop(srv);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
    return 0;
}
