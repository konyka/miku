#ifndef MIKU_ATOMIC_H
#define MIKU_ATOMIC_H

#include "miku_common.h"

#if defined(MIKU_HAS_STDATOMIC)

typedef atomic_int      miku_atomic_int_t;
typedef atomic_int_fast64_t miku_atomic_int64_t;
typedef atomic_uintptr_t miku_atomic_ptr_t;
typedef atomic_bool     miku_atomic_bool_t;

#define miku_atomic_load(p)           atomic_load(p)
#define miku_atomic_store(p, v)       atomic_store(p, v)
#define miku_atomic_fetch_add(p, v)   atomic_fetch_add(p, v)
#define miku_atomic_fetch_sub(p, v)   atomic_fetch_sub(p, v)
#define miku_atomic_exchange(p, v)    atomic_exchange(p, v)

static inline bool miku_atomic_cas(miku_atomic_int_t *p, int *expected, int desired) {
    return atomic_compare_exchange_strong(p, expected, desired);
}

#elif defined(MIKU_HAS_GCC_ATOMIC)

typedef volatile int   miku_atomic_int_t;
typedef volatile int64_t miku_atomic_int64_t;
typedef volatile uintptr_t miku_atomic_ptr_t;
typedef volatile int   miku_atomic_bool_t;

#define miku_atomic_load(p)           __atomic_load_n(p, __ATOMIC_SEQ_CST)
#define miku_atomic_store(p, v)       __atomic_store_n(p, v, __ATOMIC_SEQ_CST)
#define miku_atomic_fetch_add(p, v)   __atomic_fetch_add(p, v, __ATOMIC_SEQ_CST)
#define miku_atomic_fetch_sub(p, v)   __atomic_fetch_sub(p, v, __ATOMIC_SEQ_CST)
#define miku_atomic_exchange(p, v)    __atomic_exchange_n(p, v, __ATOMIC_SEQ_CST)

static inline bool miku_atomic_cas(miku_atomic_int_t *p, int *expected, int desired) {
    return __atomic_compare_exchange_n(p, expected, desired, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
}

#else
#error "No atomic operations available"
#endif

static inline bool miku_atomic_flag_test_and_set(miku_atomic_bool_t *f) {
    return miku_atomic_exchange(f, 1);
}

static inline void miku_atomic_flag_clear(miku_atomic_bool_t *f) {
    miku_atomic_store(f, 0);
}

#endif
