#include "miku_coroutine.h"
#include <stdlib.h>
#include <string.h>

static void coro_entry(unsigned int hi, unsigned int lo) {
    miku_coro_t *coro = (miku_coro_t *)((uintptr_t)hi << 32 | (uintptr_t)lo);
    coro->fn(coro->arg);
    coro->state = MK_CORO_DEAD;
    setcontext(coro->caller);
}

miku_coro_t *miku_coro_create(miku_coro_fn fn, void *arg, size_t stack_size) {
    if (!fn) return NULL;
    if (stack_size < 8192) stack_size = 8192;
    stack_size = MIKU_ROUND_UP(stack_size, 4096);

    miku_coro_t *coro = (miku_coro_t *)calloc(1, sizeof(*coro));
    if (!coro) return NULL;

    coro->stack = (uint8_t *)malloc(stack_size);
    if (!coro->stack) { free(coro); return NULL; }

    coro->fn = fn;
    coro->arg = arg;
    coro->stack_size = stack_size;
    coro->state = MK_CORO_READY;
    coro->wait_fd = -1;

    getcontext(&coro->ctx);
    coro->ctx.uc_stack.ss_sp = coro->stack;
    coro->ctx.uc_stack.ss_size = stack_size;
    coro->ctx.uc_link = NULL;

    uintptr_t ptr = (uintptr_t)coro;
    unsigned int hi = (unsigned int)(ptr >> 32);
    unsigned int lo = (unsigned int)(ptr & 0xFFFFFFFF);
    makecontext(&coro->ctx, (void (*)(void))coro_entry, 2, hi, lo);

    return coro;
}

void miku_coro_resume(miku_coro_t *coro) {
    if (!coro || coro->state == MK_CORO_DEAD) return;

    ucontext_t caller;
    coro->caller = &caller;
    coro->state = MK_CORO_RUNNING;
    swapcontext(&caller, &coro->ctx);
}

void miku_coro_yield(miku_coro_t *coro) {
    if (!coro || coro->state == MK_CORO_DEAD) return;
    coro->state = MK_CORO_SUSPENDED;
    swapcontext(&coro->ctx, coro->caller);
}

void miku_coro_destroy(miku_coro_t *coro) {
    if (!coro) return;
    free(coro->stack);
    free(coro);
}
