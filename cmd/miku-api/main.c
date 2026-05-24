#include "miku_common.h"
#include "miku_log.h"
#include "miku_config.h"

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
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            config_path = argv[++i];
        }
    }

    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);

    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-api starting (config: %s)", config_path);

    MK_LOG_INFO("miku-api shutting down");
    return 0;
}
