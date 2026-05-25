#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_push.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 300);
    MK_LOG_INFO("miku-push starting");

    miku_push_t *p = miku_push_create();
    if (!p) { MK_LOG_ERROR("Failed to create push service"); return 1; }
    miku_push_start(p);
    MK_LOG_INFO("miku-push ready");
    miku_graceful_wait(&g_graceful, NULL, NULL);

    MK_LOG_INFO("miku-push shutting down");
    miku_push_stop(p);
    miku_push_destroy(p);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
