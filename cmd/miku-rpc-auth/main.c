#include "miku_common.h"
#include "miku_log.h"
#include "miku_auth.h"
#include "miku_rpc_server.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    int port = -1;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
        else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);
    if (port < 0) port = sc.rpc_auth_port;

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 300);
    MK_LOG_INFO("miku-rpc-auth starting on :%d", port);

    miku_auth_service_t *svc = miku_auth_service_create();
    if (!svc) { MK_LOG_ERROR("Failed to create auth service"); return 1; }

    miku_rpc_server_t *srv = miku_rpc_server_create(svc,
        (miku_rpc_dispatch_fn)miku_auth_handle_rpc, port);
    if (!srv || miku_rpc_server_start(srv) != 0) {
        MK_LOG_ERROR("Failed to start RPC server");
        miku_auth_service_destroy(svc); return 1;
    }

    MK_LOG_INFO("miku-rpc-auth ready");
    while (miku_graceful_running(&g_graceful)) { miku_rpc_server_poll(srv, 100); }

    MK_LOG_INFO("miku-rpc-auth shutting down");
    miku_rpc_server_stop(srv);
    miku_rpc_server_destroy(srv);
    miku_auth_service_destroy(svc);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
