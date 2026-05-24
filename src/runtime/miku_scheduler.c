#include "miku_scheduler.h"
#include "miku_thread.h"
#include "miku_log.h"
#include "miku_list.h"
#include <stdlib.h>

typedef struct {
    miku_list_node_t node;
    miku_coro_t     *coro;
} ready_entry_t;

struct miku_scheduler_s {
    miku_threadpool_t *pool;
    miku_coro_t      **coros;
    size_t             coro_count;
    size_t             coro_cap;
    size_t             stack_size;
    miku_mutex_t       ready_lock;
    miku_cond_t        ready_cond;
    miku_list_node_t   ready_list;
    atomic_bool        running;
};

static void sched_coro_entry(void *arg) {
    miku_coro_t *coro = (miku_coro_t *)arg;
    miku_coro_yield(coro);
    coro->fn(coro->arg);
    coro->state = MK_CORO_DEAD;
}

miku_scheduler_t *miku_scheduler_create(int num_workers, size_t coro_stack_size) {
    miku_scheduler_t *s = (miku_scheduler_t *)calloc(1, sizeof(*s));
    if (!s) return NULL;

    s->pool = miku_threadpool_create(num_workers);
    if (!s->pool) { free(s); return NULL; }

    s->coro_cap = 1024;
    s->coros = (miku_coro_t **)calloc(s->coro_cap, sizeof(miku_coro_t *));
    s->stack_size = coro_stack_size > 0 ? coro_stack_size : 32768;

    miku_mutex_init(&s->ready_lock);
    miku_cond_init(&s->ready_cond);
    miku_list_init(&s->ready_list);
    atomic_init(&s->running, false);

    return s;
}

miku_coro_t *miku_scheduler_spawn(miku_scheduler_t *sched, miku_coro_fn fn, void *arg) {
    if (!sched || !fn) return NULL;

    miku_coro_t *coro = miku_coro_create(fn, arg, sched->stack_size);
    if (!coro) return NULL;

    if (sched->coro_count >= sched->coro_cap) {
        sched->coro_cap *= 2;
        sched->coros = (miku_coro_t **)realloc(sched->coros, sched->coro_cap * sizeof(miku_coro_t *));
    }
    coro->id = (int64_t)sched->coro_count;
    sched->coros[sched->coro_count++] = coro;

    miku_scheduler_wakeup(sched, coro);
    return coro;
}

void miku_scheduler_wakeup(miku_scheduler_t *sched, miku_coro_t *coro) {
    if (!sched || !coro) return;

    ready_entry_t *entry = (ready_entry_t *)malloc(sizeof(*entry));
    entry->coro = coro;
    miku_list_init(&entry->node);

    miku_mutex_lock(&sched->ready_lock);
    miku_list_push_back(&sched->ready_list, &entry->node);
    miku_mutex_unlock(&sched->ready_lock);
    miku_cond_signal(&sched->ready_cond);
}

static void run_coro_task(void *arg) {
    miku_coro_t *coro = (miku_coro_t *)arg;
    if (miku_coro_alive(coro)) {
        miku_coro_resume(coro);
    }
}

void miku_scheduler_run(miku_scheduler_t *sched) {
    if (!sched) return;
    atomic_store(&sched->running, true);

    while (atomic_load(&sched->running)) {
        miku_mutex_lock(&sched->ready_lock);

        while (miku_list_empty(&sched->ready_list) && atomic_load(&sched->running)) {
            miku_cond_timedwait(&sched->ready_cond, &sched->ready_lock, 100);
        }

        miku_list_node_t *node;
        ready_entry_t *entries[256];
        int count = 0;

        while (!miku_list_empty(&sched->ready_list) && count < 256) {
            node = sched->ready_list.next;
            miku_list_remove(node);
            entries[count++] = MIKU_CONTAINER_OF(node, ready_entry_t, node);
        }
        miku_mutex_unlock(&sched->ready_lock);

        for (int i = 0; i < count; i++) {
            miku_coro_t *coro = entries[i]->coro;
            free(entries[i]);
            if (miku_coro_alive(coro)) {
                miku_threadpool_submit(sched->pool, run_coro_task, coro);
            }
        }
    }
}

void miku_scheduler_stop(miku_scheduler_t *sched) {
    if (!sched) return;
    atomic_store(&sched->running, false);
    miku_cond_broadcast(&sched->ready_cond);
}

void miku_scheduler_destroy(miku_scheduler_t *sched) {
    if (!sched) return;
    miku_scheduler_stop(sched);

    if (sched->pool) {
        miku_threadpool_wait_idle(sched->pool);
        miku_threadpool_destroy(sched->pool);
    }

    for (size_t i = 0; i < sched->coro_count; i++) {
        miku_coro_destroy(sched->coros[i]);
    }
    free(sched->coros);

    miku_list_node_t *node, *tmp;
    miku_list_foreach_safe(&sched->ready_list, node, tmp) {
        ready_entry_t *e = MIKU_CONTAINER_OF(node, ready_entry_t, node);
        free(e);
    }

    miku_mutex_destroy(&sched->ready_lock);
    miku_cond_destroy(&sched->ready_cond);
    free(sched);
}

size_t miku_scheduler_coro_count(const miku_scheduler_t *sched) {
    return sched ? sched->coro_count : 0;
}
