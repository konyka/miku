#include "miku_async.h"
#include <stdlib.h>

miku_async_result_t *miku_await(miku_async_fn fn, void *arg) {
    if (!fn) return NULL;
    miku_async_result_t *r = fn(arg);
    return r;
}

void *miku_async_get_result(miku_async_result_t *r) {
    return r ? r->result : NULL;
}

void miku_async_result_destroy(miku_async_result_t *r) {
    free(r);
}
