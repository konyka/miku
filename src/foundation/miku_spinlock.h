#ifndef MIKU_SPINLOCK_H
#define MIKU_SPINLOCK_H

#include "miku_common.h"

#if defined(MIKU_HAS_STDATOMIC)
typedef atomic_int miku_spinlock_t;
#elif defined(MIKU_HAS_GCC_ATOMIC)
typedef volatile int miku_spinlock_t;
#else
typedef volatile int miku_spinlock_t;
#endif

static inline void miku_spinlock_lock(miku_spinlock_t *lock) {
#if defined(MIKU_HAS_STDATOMIC)
    while (atomic_exchange_explicit(lock, 1, memory_order_acquire)) {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#elif defined(__aarch64__)
        __asm__ __volatile__("yield" ::: "memory");
#endif
    }
#else
    while (__sync_lock_test_and_set(lock, 1)) {
#if defined(__x86_64__) || defined(_M_X64)
        __builtin_ia32_pause();
#endif
    }
#endif
}

static inline void miku_spinlock_unlock(miku_spinlock_t *lock) {
#if defined(MIKU_HAS_STDATOMIC)
    atomic_store_explicit(lock, 0, memory_order_release);
#else
    __sync_lock_release(lock);
#endif
}

static inline bool miku_spinlock_trylock(miku_spinlock_t *lock) {
#if defined(MIKU_HAS_STDATOMIC)
    return atomic_exchange_explicit(lock, 1, memory_order_acquire) == 0;
#else
    return __sync_lock_test_and_set(lock, 1) == 0;
#endif
}

#endif
