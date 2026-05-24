#ifndef MIKU_ASYNC_H
#define MIKU_ASYNC_H

#include "miku_common.h"
#include "miku_coroutine.h"
#include "miku_error.h"

typedef struct {
    miku_coro_t *coro;
    void        *result;
    miku_error_t error;
} miku_async_result_t;

typedef miku_async_result_t *(*miku_async_fn)(void *arg);

MIKU_API miku_async_result_t *miku_await(miku_async_fn fn, void *arg);
MIKU_API void *miku_async_get_result(miku_async_result_t *r);
MIKU_API void  miku_async_result_destroy(miku_async_result_t *r);

#endif
