#ifndef MIKU_THREAD_H
#define MIKU_THREAD_H

#include "miku_common.h"

typedef pthread_mutex_t miku_mutex_t;
typedef pthread_cond_t  miku_cond_t;
typedef pthread_rwlock_t miku_rwlock_t;

MIKU_API int miku_mutex_init(miku_mutex_t *m);
MIKU_API int miku_mutex_lock(miku_mutex_t *m);
MIKU_API int miku_mutex_unlock(miku_mutex_t *m);
MIKU_API int miku_mutex_destroy(miku_mutex_t *m);

MIKU_API int miku_cond_init(miku_cond_t *c);
MIKU_API int miku_cond_wait(miku_cond_t *c, miku_mutex_t *m);
MIKU_API int miku_cond_timedwait(miku_cond_t *c, miku_mutex_t *m, int64_t ms);
MIKU_API int miku_cond_signal(miku_cond_t *c);
MIKU_API int miku_cond_broadcast(miku_cond_t *c);
MIKU_API int miku_cond_destroy(miku_cond_t *c);

MIKU_API int miku_rwlock_init(miku_rwlock_t *rw);
MIKU_API int miku_rwlock_rdlock(miku_rwlock_t *rw);
MIKU_API int miku_rwlock_wrlock(miku_rwlock_t *rw);
MIKU_API int miku_rwlock_unlock(miku_rwlock_t *rw);
MIKU_API int miku_rwlock_destroy(miku_rwlock_t *rw);

MIKU_API int miku_thread_setname(const char *name);

#endif
