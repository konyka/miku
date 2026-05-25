#include "miku_common.h"
#include "miku_log.h"
#include "miku_push.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>

static volatile int g_running = 1;
static void signal_handler(int sig) { (void)sig; g_running = 0; }

int main(int argc, char **argv) {
    const char *config_path = "config/";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_path = argv[++i];
    }
    signal(SIGTERM, signal_handler);
    signal(SIGINT,  signal_handler);
    miku_log_init(NULL, MK_LOG_DEBUG);
    MK_LOG_INFO("miku-push starting (config: %s)", config_path);

    miku_push_t *p = miku_push_create();
    if (!p) { MK_LOG_ERROR("Failed to create push service"); return 1; }
    miku_push_start(p);
    MK_LOG_INFO("miku-push ready");

    while (g_running) { usleep(100000); }

    MK_LOG_INFO("miku-push shutting down");
    miku_push_stop(p);
    miku_push_destroy(p);
    return 0;
}
