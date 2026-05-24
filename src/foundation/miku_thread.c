#include "miku_thread.h"

int miku_mutex_init(miku_mutex_t *m)    { return pthread_mutex_init(m, NULL); }
int miku_mutex_lock(miku_mutex_t *m)    { return pthread_mutex_lock(m); }
int miku_mutex_unlock(miku_mutex_t *m)  { return pthread_mutex_unlock(m); }
int miku_mutex_destroy(miku_mutex_t *m) { return pthread_mutex_destroy(m); }

int miku_cond_init(miku_cond_t *c)      { return pthread_cond_init(c, NULL); }
int miku_cond_wait(miku_cond_t *c, miku_mutex_t *m) { return pthread_cond_wait(c, m); }

int miku_cond_timedwait(miku_cond_t *c, miku_mutex_t *m, int64_t ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec  += ms / 1000;
    ts.tv_nsec += (ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) { ts.tv_sec++; ts.tv_nsec -= 1000000000; }
    return pthread_cond_timedwait(c, m, &ts);
}

int miku_cond_signal(miku_cond_t *c)     { return pthread_cond_signal(c); }
int miku_cond_broadcast(miku_cond_t *c)  { return pthread_cond_broadcast(c); }
int miku_cond_destroy(miku_cond_t *c)    { return pthread_cond_destroy(c); }

int miku_rwlock_init(miku_rwlock_t *rw)   { return pthread_rwlock_init(rw, NULL); }
int miku_rwlock_rdlock(miku_rwlock_t *rw) { return pthread_rwlock_rdlock(rw); }
int miku_rwlock_wrlock(miku_rwlock_t *rw) { return pthread_rwlock_wrlock(rw); }
int miku_rwlock_unlock(miku_rwlock_t *rw) { return pthread_rwlock_unlock(rw); }
int miku_rwlock_destroy(miku_rwlock_t *rw){ return pthread_rwlock_destroy(rw); }

int miku_thread_setname(const char *name) {
#if defined(MIKU_LINUX)
    return pthread_setname_np(pthread_self(), name);
#elif defined(MIKU_MACOS)
    return pthread_setname_np(name);
#else
    (void)name;
    return 0;
#endif
}
