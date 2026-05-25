#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_crontask.h"

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
    MK_LOG_INFO("miku-crontask starting");

    miku_crontask_t *ct = miku_crontask_create();
    if (!ct) { MK_LOG_ERROR("Failed to create crontask"); return 1; }
    miku_crontask_start(ct);
    MK_LOG_INFO("miku-crontask ready");

    while (miku_graceful_running(&g_graceful)) {
        miku_crontask_tick(ct);
        usleep(50000);
    }

    MK_LOG_INFO("miku-crontask shutting down");
    miku_crontask_stop(ct);
    miku_crontask_destroy(ct);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
