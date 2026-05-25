#ifndef MIKU_CRONTASK_H
#define MIKU_CRONTASK_H

#include "miku_common.h"

#define MK_CRON_MAX_TASKS 256

typedef void (*miku_cron_fn)(void *ctx);

typedef struct {
    char          name[64];
    miku_cron_fn  fn;
    void         *ctx;
    int64_t       interval_ms;
    int64_t       last_run;
    int           run_count;
    bool          enabled;
} miku_cron_task_t;

typedef struct miku_crontask_s miku_crontask_t;

MIKU_API miku_crontask_t *miku_crontask_create(void);
MIKU_API void miku_crontask_destroy(miku_crontask_t *ct);
MIKU_API int  miku_crontask_start(miku_crontask_t *ct);
MIKU_API int  miku_crontask_stop(miku_crontask_t *ct);

MIKU_API int  miku_crontask_add(miku_crontask_t *ct, const char *name,
                                 miku_cron_fn fn, void *ctx, int64_t interval_ms);
MIKU_API int  miku_crontask_remove(miku_crontask_t *ct, const char *name);
MIKU_API int  miku_crontask_tick(miku_crontask_t *ct);
MIKU_API int  miku_crontask_task_count(miku_crontask_t *ct);

#endif
