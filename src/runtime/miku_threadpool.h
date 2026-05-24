#ifndef MIKU_THREADPOOL_H
#define MIKU_THREADPOOL_H

#include "miku_common.h"
#include "miku_coroutine.h"

typedef struct miku_threadpool_s miku_threadpool_t;

typedef void (*miku_task_fn)(void *arg);

typedef struct miku_task_s {
    miku_task_fn  fn;
    void         *arg;
    struct miku_task_s *next;
} miku_task_t;

MIKU_API miku_threadpool_t *miku_threadpool_create(int num_workers);
MIKU_API int  miku_threadpool_submit(miku_threadpool_t *pool, miku_task_fn fn, void *arg);
MIKU_API int  miku_threadpool_submit_coro(miku_threadpool_t *pool, miku_coro_t *coro);
MIKU_API void miku_threadpool_destroy(miku_threadpool_t *pool);
MIKU_API int  miku_threadpool_worker_count(const miku_threadpool_t *pool);
MIKU_API void miku_threadpool_wait_idle(miku_threadpool_t *pool);

#endif
