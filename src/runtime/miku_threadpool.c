#include "miku_threadpool.h"
#include "miku_thread.h"
#include "miku_log.h"
#include "miku_atomic.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    miku_task_t *head;
    miku_task_t *tail;
    size_t       count;
} task_queue_t;

struct miku_threadpool_s {
    pthread_t     *workers;
    int            num_workers;
    task_queue_t   global_queue;
    miku_mutex_t   queue_lock;
    miku_cond_t    queue_cond;
    miku_cond_t    idle_cond;
    atomic_int     pending;
    atomic_int     idle_workers;
    atomic_bool    shutdown;
};

static void tq_push(task_queue_t *q, miku_task_fn fn, void *arg) {
    miku_task_t *t = (miku_task_t *)malloc(sizeof(*t));
    t->fn = fn;
    t->arg = arg;
    t->next = NULL;
    if (q->tail) q->tail->next = t;
    else q->head = t;
    q->tail = t;
    q->count++;
}

static miku_task_t *tq_pop(task_queue_t *q) {
    if (!q->head) return NULL;
    miku_task_t *t = q->head;
    q->head = t->next;
    if (!q->head) q->tail = NULL;
    q->count--;
    return t;
}

static void *worker_loop(void *arg) {
    miku_threadpool_t *pool = (miku_threadpool_t *)arg;
    miku_atomic_fetch_add(&pool->idle_workers, 1);

    while (true) {
        miku_mutex_lock(&pool->queue_lock);

        while (!pool->global_queue.head && !miku_atomic_load(&pool->shutdown)) {
            miku_cond_wait(&pool->queue_cond, &pool->queue_lock);
        }

        if (miku_atomic_load(&pool->shutdown) && !pool->global_queue.head) {
            miku_atomic_fetch_sub(&pool->idle_workers, 1);
            miku_mutex_unlock(&pool->queue_lock);
            return NULL;
        }

        miku_task_t *task = tq_pop(&pool->global_queue);
        if (task) {
            miku_atomic_fetch_sub(&pool->pending, 1);
            miku_atomic_fetch_sub(&pool->idle_workers, 1);
        }
        miku_mutex_unlock(&pool->queue_lock);

        if (task) {
            task->fn(task->arg);
            free(task);
            miku_atomic_fetch_add(&pool->idle_workers, 1);
            if (miku_atomic_load(&pool->pending) == 0) {
                miku_cond_broadcast(&pool->idle_cond);
            }
        }
    }
}

miku_threadpool_t *miku_threadpool_create(int num_workers) {
    if (num_workers <= 0) num_workers = 4;

    miku_threadpool_t *pool = (miku_threadpool_t *)calloc(1, sizeof(*pool));
    if (!pool) return NULL;

    pool->num_workers = num_workers;
    pool->workers = (pthread_t *)calloc((size_t)num_workers, sizeof(pthread_t));
    miku_mutex_init(&pool->queue_lock);
    miku_cond_init(&pool->queue_cond);
    miku_cond_init(&pool->idle_cond);
    atomic_init(&pool->pending, 0);
    atomic_init(&pool->idle_workers, 0);
    atomic_init(&pool->shutdown, false);

    for (int i = 0; i < num_workers; i++) {
        pthread_create(&pool->workers[i], NULL, worker_loop, pool);
    }
    return pool;
}

int miku_threadpool_submit(miku_threadpool_t *pool, miku_task_fn fn, void *arg) {
    if (!pool || !fn) return -1;
    if (miku_atomic_load(&pool->shutdown)) return -1;

    miku_mutex_lock(&pool->queue_lock);
    tq_push(&pool->global_queue, fn, arg);
    miku_atomic_fetch_add(&pool->pending, 1);
    miku_mutex_unlock(&pool->queue_lock);
    miku_cond_signal(&pool->queue_cond);
    return 0;
}

static void coro_wrapper(void *arg) {
    (void)arg;
}

int miku_threadpool_submit_coro(miku_threadpool_t *pool, miku_coro_t *coro) {
    return miku_threadpool_submit(pool, coro_wrapper, coro);
}

void miku_threadpool_destroy(miku_threadpool_t *pool) {
    if (!pool) return;

    miku_atomic_store(&pool->shutdown, true);
    miku_cond_broadcast(&pool->queue_cond);

    for (int i = 0; i < pool->num_workers; i++) {
        pthread_join(pool->workers[i], NULL);
    }

    miku_task_t *t;
    while ((t = tq_pop(&pool->global_queue)) != NULL) free(t);

    miku_mutex_destroy(&pool->queue_lock);
    miku_cond_destroy(&pool->queue_cond);
    miku_cond_destroy(&pool->idle_cond);
    free(pool->workers);
    free(pool);
}

int miku_threadpool_worker_count(const miku_threadpool_t *pool) {
    return pool ? pool->num_workers : 0;
}

void miku_threadpool_wait_idle(miku_threadpool_t *pool) {
    if (!pool) return;
    miku_mutex_lock(&pool->queue_lock);
    while (miku_atomic_load(&pool->pending) > 0) {
        miku_cond_timedwait(&pool->idle_cond, &pool->queue_lock, 100);
    }
    miku_mutex_unlock(&pool->queue_lock);
}
