#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msggateway.h"

#include <stdio.h>
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
    if (port < 0) port = sc.ws_port;

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 500);
    MK_LOG_INFO("miku-msggateway starting on :%d", port);

    miku_msggw_t *gw = miku_msggw_create(port);
    if (!gw) { MK_LOG_ERROR("Failed to create message gateway"); return 1; }

    miku_msggw_start(gw);
    MK_LOG_INFO("miku-msggateway ready (ws://0.0.0.0:%d)", port);

    while (miku_graceful_running(&g_graceful)) {
        miku_msggw_poll(gw, 100);
    }

    MK_LOG_INFO("miku-msggateway shutting down");
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
