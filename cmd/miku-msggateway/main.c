#include "miku_common.h"
#include "miku_log.h"
#include "miku_msggateway.h"

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
    int port = 10001;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        }
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-msggateway starting on :%d", port);

    miku_msggw_t *gw = miku_msggw_create(port);
    if (!gw) {
        MK_LOG_ERROR("Failed to create message gateway");
        return 1;
    }

    miku_msggw_start(gw);
    MK_LOG_INFO("miku-msggateway ready (ws://0.0.0.0:%d)", port);

    while (g_running) {
        miku_msggw_poll(gw, 100);
    }

    MK_LOG_INFO("miku-msggateway shutting down");
    miku_msggw_stop(gw);
    miku_msggw_destroy(gw);
    return 0;
}
