#include "miku_crontask.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

struct miku_crontask_s {
    miku_cron_task_t tasks[MK_CRON_MAX_TASKS];
    int              task_count;
    int              running;
    int64_t          total_ticks;
};

miku_crontask_t *miku_crontask_create(void) {
    return (miku_crontask_t *)calloc(1, sizeof(miku_crontask_t));
}
void miku_crontask_destroy(miku_crontask_t *ct) { free(ct); }

int miku_crontask_start(miku_crontask_t *ct) {
    if (!ct) return -1;
    ct->running = 1;
    MK_LOG_INFO("Crontask: started (%d tasks registered)", ct->task_count);
    return 0;
}
int miku_crontask_stop(miku_crontask_t *ct) {
    if (!ct) return -1;
    ct->running = 0;
    MK_LOG_INFO("Crontask: stopped (total ticks: %ld)", (long)ct->total_ticks);
    return 0;
}

int miku_crontask_add(miku_crontask_t *ct, const char *name,
                       miku_cron_fn fn, void *ctx, int64_t interval_ms) {
    if (!ct || !name || !fn || ct->task_count >= MK_CRON_MAX_TASKS) return -1;
    miku_cron_task_t *t = &ct->tasks[ct->task_count++];
    strncpy(t->name, name, sizeof(t->name) - 1);
    t->fn = fn;
    t->ctx = ctx;
    t->interval_ms = interval_ms;
    t->last_run = 0;
    t->run_count = 0;
    t->enabled = true;
    return 0;
}

int miku_crontask_remove(miku_crontask_t *ct, const char *name) {
    if (!ct || !name) return -1;
    for (int i = 0; i < ct->task_count; i++) {
        if (strcmp(ct->tasks[i].name, name) == 0) {
            ct->tasks[i] = ct->tasks[--ct->task_count];
            return 0;
        }
    }
    return -1;
}

int miku_crontask_tick(miku_crontask_t *ct) {
    if (!ct || !ct->running) return 0;
    int64_t now = miku_timestamp_ms();
    int executed = 0;
    for (int i = 0; i < ct->task_count; i++) {
        miku_cron_task_t *t = &ct->tasks[i];
        if (!t->enabled) continue;
        if (t->last_run == 0 || (now - t->last_run) >= t->interval_ms) {
            t->fn(t->ctx);
            t->last_run = now;
            t->run_count++;
            executed++;
        }
    }
    ct->total_ticks++;
    return executed;
}

int miku_crontask_task_count(miku_crontask_t *ct) {
    if (!ct) return 0;
    return ct->task_count;
}
