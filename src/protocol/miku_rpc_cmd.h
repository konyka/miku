#ifndef MIKU_RPC_CMD_H
#define MIKU_RPC_CMD_H

#include "miku_common.h"
#include "miku_log.h"
#include "miku_rpc_server.h"
#include "miku_json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#define MIKU_RPC_MAIN(SVC_TYPE, create_fn, destroy_fn, handle_rpc_fn, default_port, svc_name) \
static volatile int g_running = 1; \
static void signal_handler(int sig) { (void)sig; g_running = 0; } \
\
int main(int argc, char **argv) { \
    int port = default_port; \
    for (int i = 1; i < argc; i++) { \
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) port = atoi(argv[++i]); \
    } \
    signal(SIGTERM, signal_handler); \
    signal(SIGINT,  signal_handler); \
    miku_log_init(NULL, MK_LOG_DEBUG); \
    MK_LOG_INFO("%s starting on :%d", svc_name, port); \
\
    SVC_TYPE *svc = create_fn(); \
    if (!svc) { MK_LOG_ERROR("Failed to create " svc_name); return 1; } \
\
    miku_rpc_server_t *srv = miku_rpc_server_create(svc, (miku_rpc_dispatch_fn)handle_rpc_fn, port); \
    if (!srv) { MK_LOG_ERROR("Failed to create RPC server"); destroy_fn(svc); return 1; } \
\
    if (miku_rpc_server_start(srv) != 0) { \
        miku_rpc_server_destroy(srv); \
        destroy_fn(svc); \
        return 1; \
    } \
\
    MK_LOG_INFO("%s ready", svc_name); \
    while (g_running) { \
        miku_rpc_server_poll(srv, 100); \
    } \
\
    MK_LOG_INFO("%s shutting down", svc_name); \
    miku_rpc_server_stop(srv); \
    miku_rpc_server_destroy(srv); \
    destroy_fn(svc); \
    return 0; \
}

#endif
