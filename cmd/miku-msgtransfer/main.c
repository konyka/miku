#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msgtransfer.h"

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
    MK_LOG_INFO("miku-msgtransfer starting (kafka: %s/%s)", sc.kafka_brokers, sc.kafka_topic);

    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    if (!mt) { MK_LOG_ERROR("Failed to create msgtransfer"); return 1; }

    miku_msgtransfer_start(mt);
    MK_LOG_INFO("miku-msgtransfer ready");
    miku_graceful_wait(&g_graceful, NULL, NULL);

    MK_LOG_INFO("miku-msgtransfer shutting down");
    miku_msgtransfer_stop(mt);
    miku_msgtransfer_destroy(mt);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
