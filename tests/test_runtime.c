#include "miku_test.h"
#include "miku_coroutine.h"
#include "miku_threadpool.h"
#include "miku_scheduler.h"
#include "miku_channel.h"
#include "miku_timer.h"
#include <unistd.h>
#include <stdatomic.h>

static atomic_int g_coro_counter;
static miku_coro_t *g_yield_coro;

static void coro_simple(void *arg) {
    int *val = (int *)arg;
    *val += 10;
}

void test_coroutine_basic(void) {
    int val = 0;
    miku_coro_t *c = miku_coro_create(coro_simple, &val, 32768);
    mk_assert_not_null(c);
    mk_assert_int_eq(MK_CORO_READY, miku_coro_state(c));
    miku_coro_resume(c);
    mk_assert_int_eq(10, val);
    mk_assert_int_eq(MK_CORO_DEAD, miku_coro_state(c));
    miku_coro_destroy(c);
}

static void coro_yield_fn(void *arg) {
    int *steps = (int *)arg;
    steps[0] = 1;
    miku_coro_yield(g_yield_coro);
    steps[1] = 2;
    miku_coro_yield(g_yield_coro);
    steps[2] = 3;
}

void test_coroutine_yield(void) {
    int steps[3] = {0, 0, 0};
    miku_coro_t *c = miku_coro_create(coro_yield_fn, steps, 32768);
    mk_assert_not_null(c);

    g_yield_coro = c;
    miku_coro_resume(c);
    mk_assert_int_eq(1, steps[0]);
    mk_assert_int_eq(0, steps[1]);

    miku_coro_resume(c);
    mk_assert_int_eq(2, steps[1]);
    mk_assert_int_eq(0, steps[2]);

    miku_coro_resume(c);
    mk_assert_int_eq(3, steps[2]);
    mk_assert_int_eq(MK_CORO_DEAD, miku_coro_state(c));
    g_yield_coro = NULL;

    miku_coro_destroy(c);
}

static void coro_nested_fn(void *arg) {
    miku_coro_t *inner = (miku_coro_t *)arg;
    miku_coro_resume(inner);
}

void test_coroutine_nested(void) {
    int val = 0;
    miku_coro_t *inner = miku_coro_create(coro_simple, &val, 32768);
    miku_coro_t *outer = miku_coro_create(coro_nested_fn, inner, 32768);
    miku_coro_resume(outer);
    mk_assert_int_eq(10, val);
    miku_coro_destroy(inner);
    miku_coro_destroy(outer);
}

static atomic_int g_tp_counter;

static void tp_task_fn(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_tp_counter, 1);
}

void test_threadpool_basic(void) {
    g_tp_counter = 0;
    miku_threadpool_t *pool = miku_threadpool_create(4);
    mk_assert_not_null(pool);
    mk_assert_int_eq(4, miku_threadpool_worker_count(pool));

    for (int i = 0; i < 100; i++) {
        miku_threadpool_submit(pool, tp_task_fn, NULL);
    }
    miku_threadpool_wait_idle(pool);
    mk_assert_int_eq(100, atomic_load(&g_tp_counter));

    miku_threadpool_destroy(pool);
}

static atomic_int g_sched_counter;

static void sched_coro_fn(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_sched_counter, 1);
}

static void *sched_run_thread(void *arg) {
    miku_scheduler_t *sched = (miku_scheduler_t *)arg;
    miku_scheduler_run(sched);
    return NULL;
}

void test_scheduler_basic(void) {
    g_sched_counter = 0;
    miku_scheduler_t *sched = miku_scheduler_create(4, 32768);
    mk_assert_not_null(sched);

    pthread_t tid;
    pthread_create(&tid, NULL, sched_run_thread, sched);
    usleep(50000);

    for (int i = 0; i < 50; i++) {
        miku_scheduler_spawn(sched, sched_coro_fn, NULL);
    }
    mk_assert_int_eq(50, (int)miku_scheduler_coro_count(sched));

    usleep(500000);
    miku_scheduler_stop(sched);
    pthread_join(tid, NULL);
    miku_scheduler_destroy(sched);
    mk_assert_int_eq(50, atomic_load(&g_sched_counter));
}

void test_channel_basic(void) {
    miku_channel_t *ch = miku_channel_create(4);
    mk_assert_not_null(ch);
    mk_assert_int_eq(0, (int)miku_channel_len(ch));
    mk_assert_false(miku_channel_closed(ch));

    miku_channel_send(ch, (void *)1L);
    miku_channel_send(ch, (void *)2L);
    miku_channel_send(ch, (void *)3L);
    mk_assert_int_eq(3, (int)miku_channel_len(ch));

    mk_assert_ptr_eq((void *)1L, miku_channel_recv(ch));
    mk_assert_ptr_eq((void *)2L, miku_channel_recv(ch));
    mk_assert_ptr_eq((void *)3L, miku_channel_recv(ch));
    mk_assert_int_eq(0, (int)miku_channel_len(ch));

    miku_channel_close(ch);
    mk_assert_true(miku_channel_closed(ch));
    miku_channel_destroy(ch);
}

static atomic_int g_timer_fired;

static void timer_fn(void *arg) {
    (void)arg;
    atomic_fetch_add(&g_timer_fired, 1);
}

void test_timer_basic(void) {
    g_timer_fired = 0;
    miku_timer_t *tm = miku_timer_create();
    mk_assert_not_null(tm);

    int64_t past = miku_timestamp_ms() - 1000;
    miku_timer_add(tm, past, timer_fn, NULL, NULL);
    miku_timer_add(tm, past, timer_fn, NULL, NULL);
    miku_timer_add(tm, past, timer_fn, NULL, NULL);

    int fired = miku_timer_process(tm);
    mk_assert_int_eq(3, fired);
    mk_assert_int_eq(3, atomic_load(&g_timer_fired));
    mk_assert_int_eq(-1, (int)miku_timer_next_deadline(tm));

    miku_timer_destroy(tm);
}

void test_timer_cancel(void) {
    miku_timer_t *tm = miku_timer_create();
    uint64_t tid;
    int64_t future = miku_timestamp_ms() + 60000;
    miku_timer_add(tm, future, timer_fn, NULL, &tid);
    mk_assert_int_eq(0, miku_timer_cancel(tm, tid));
    int fired = miku_timer_process(tm);
    mk_assert_int_eq(0, fired);
    miku_timer_destroy(tm);
}

void run_runtime_tests(void) {
    printf("── Miku Runtime Tests ───────────────────\n\n");

    mk_run_test(test_coroutine_basic);
    mk_run_test(test_coroutine_yield);
    mk_run_test(test_coroutine_nested);
    mk_run_test(test_threadpool_basic);
    mk_run_test(test_channel_basic);
    mk_run_test(test_timer_basic);
    mk_run_test(test_timer_cancel);
    mk_run_test(test_scheduler_basic);
}
