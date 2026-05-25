#include "miku_common.h"
#include "miku_log.h"
#include "miku_conversation.h"
#include "miku_rpc_server.h"
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

static volatile int g_running = 1;
static void signal_handler(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    int port = 10180;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]);
    }
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-rpc-conversation starting on :%d", port);

    miku_conv_service_t *svc = miku_conv_service_create();
    if (!svc) { MK_LOG_ERROR("Failed to create conversation service"); return 1; }

    miku_rpc_server_t *srv = miku_rpc_server_create(svc,
        (miku_rpc_dispatch_fn)miku_conv_handle_rpc, port);
    if (!srv || miku_rpc_server_start(srv) != 0) {
        miku_conv_service_destroy(svc); return 1;
    }

    MK_LOG_INFO("miku-rpc-conversation ready");
    while (g_running) { miku_rpc_server_poll(srv, 100); }

    miku_rpc_server_stop(srv);
    miku_rpc_server_destroy(srv);
    miku_conv_service_destroy(svc);
    return 0;
}
