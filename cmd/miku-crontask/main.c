#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_crontask.h"
#include "miku_cron_tasks.h"
#include "miku_msg_store.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static miku_graceful_t g_graceful;
static miku_cron_tasks_t *g_tasks;
static miku_msg_store_t *g_store;

static void cron_delete_msg(void *ctx) {
    miku_cron_tasks_t *t = (miku_cron_tasks_t *)ctx;
    miku_cron_delete_expired_msgs(t, 180);
}

static void cron_clear_s3(void *ctx) {
    miku_cron_tasks_t *t = (miku_cron_tasks_t *)ctx;
    miku_cron_clear_s3_files(t, 30);
}

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

    g_store = miku_msg_store_create(NULL);
    g_tasks = miku_cron_tasks_create();
    if (!g_tasks || !g_store) {
        MK_LOG_ERROR("Failed to create cron tasks / msg_store");
        return 1;
    }
    miku_cron_tasks_set_msg_store(g_tasks, g_store);

    miku_crontask_t *sched = miku_crontask_create();
    if (!sched) {
        MK_LOG_ERROR("Failed to create crontask scheduler");
        miku_cron_tasks_destroy(g_tasks);
        miku_msg_store_destroy(g_store);
        return 1;
    }

    miku_crontask_add(sched, "deleteMsg",  cron_delete_msg, g_tasks, 86400000);
    miku_crontask_add(sched, "clearS3",    cron_clear_s3,   g_tasks, 604800000);

    miku_crontask_start(sched);
    MK_LOG_INFO("miku-crontask ready — %d tasks, msg_store bound (count=%d)",
                miku_crontask_task_count(sched), miku_msg_store_count(g_store));

    while (miku_graceful_running(&g_graceful)) {
        miku_crontask_tick(sched);
        usleep(50000);
    }

    MK_LOG_INFO("miku-crontask shutting down");
    MK_LOG_INFO("  deleteMsg last_run=%ld total_deleted=%ld",
                (long)miku_cron_get_last_run(g_tasks, "deleteMsg"),
                (long)miku_cron_total_msgs_deleted(g_tasks));
    MK_LOG_INFO("  clearS3   last_run=%ld", (long)miku_cron_get_last_run(g_tasks, "clearS3"));

    miku_crontask_stop(sched);
    miku_crontask_destroy(sched);
    miku_cron_tasks_destroy(g_tasks);
    miku_msg_store_destroy(g_store);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
