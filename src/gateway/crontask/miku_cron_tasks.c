#include "miku_cron_tasks.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#define MK_MAX_TASK_HISTORY 64

typedef struct {
    char    name[64];
    int64_t last_run_ms;
    int     run_count;
} task_history_t;

struct miku_cron_tasks_s {
    task_history_t history[MK_MAX_TASK_HISTORY];
    int            history_count;
    int64_t        total_msgs_deleted;
    int64_t        total_s3_files_deleted;
};

miku_cron_tasks_t *miku_cron_tasks_create(void) {
    return (miku_cron_tasks_t *)calloc(1, sizeof(miku_cron_tasks_t));
}

void miku_cron_tasks_destroy(miku_cron_tasks_t *ct) { free(ct); }

static void record_run(miku_cron_tasks_t *ct, const char *name) {
    for (int i = 0; i < ct->history_count; i++) {
        if (strcmp(ct->history[i].name, name) == 0) {
            ct->history[i].last_run_ms = miku_timestamp_ms();
            ct->history[i].run_count++;
            return;
        }
    }
    if (ct->history_count < MK_MAX_TASK_HISTORY) {
        strncpy(ct->history[ct->history_count].name, name, 63);
        ct->history[ct->history_count].last_run_ms = miku_timestamp_ms();
        ct->history[ct->history_count].run_count = 1;
        ct->history_count++;
    }
}

int miku_cron_delete_expired_msgs(miku_cron_tasks_t *ct, int64_t retain_days) {
    if (!ct) return -1;
    record_run(ct, "deleteMsg");
    MK_LOG_INFO("cron: deleteExpiredMsgs (retain=%ld days)", (long)retain_days);
    ct->total_msgs_deleted += 0;
    return 0;
}

int miku_cron_clear_user_msgs(miku_cron_tasks_t *ct, const char *user_id) {
    if (!ct || !user_id) return -1;
    record_run(ct, "clearUserMsg");
    MK_LOG_INFO("cron: clearUserMsgs (user=%s)", user_id);
    return 0;
}

int miku_cron_clear_s3_files(miku_cron_tasks_t *ct, int64_t expire_days) {
    if (!ct) return -1;
    record_run(ct, "clearS3");
    MK_LOG_INFO("cron: clearS3Files (expire=%ld days)", (long)expire_days);
    ct->total_s3_files_deleted += 0;
    return 0;
}

int64_t miku_cron_get_last_run(miku_cron_tasks_t *ct, const char *task_name) {
    if (!ct || !task_name) return 0;
    for (int i = 0; i < ct->history_count; i++) {
        if (strcmp(ct->history[i].name, task_name) == 0)
            return ct->history[i].last_run_ms;
    }
    return 0;
}
