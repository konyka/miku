#ifndef MIKU_SCHEDULER_H
#define MIKU_SCHEDULER_H

#include "miku_common.h"
#include "miku_coroutine.h"
#include "miku_threadpool.h"

typedef struct miku_scheduler_s miku_scheduler_t;

MIKU_API miku_scheduler_t *miku_scheduler_create(int num_workers, size_t coro_stack_size);
MIKU_API miku_coro_t      *miku_scheduler_spawn(miku_scheduler_t *sched, miku_coro_fn fn, void *arg);
MIKU_API void              miku_scheduler_run(miku_scheduler_t *sched);
MIKU_API void              miku_scheduler_stop(miku_scheduler_t *sched);
MIKU_API void              miku_scheduler_destroy(miku_scheduler_t *sched);
MIKU_API size_t            miku_scheduler_coro_count(const miku_scheduler_t *sched);
MIKU_API void              miku_scheduler_wakeup(miku_scheduler_t *sched, miku_coro_t *coro);

#endif
