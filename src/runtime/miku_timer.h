#ifndef MIKU_TIMER_H
#define MIKU_TIMER_H

#include "miku_common.h"

typedef struct miku_timer_s miku_timer_t;
typedef void (*miku_timer_fn)(void *arg);

MIKU_API miku_timer_t *miku_timer_create(void);
MIKU_API int   miku_timer_add(miku_timer_t *tm, int64_t deadline_ms,
                               miku_timer_fn fn, void *arg, uint64_t *timer_id);
MIKU_API int   miku_timer_cancel(miku_timer_t *tm, uint64_t timer_id);
MIKU_API int   miku_timer_process(miku_timer_t *tm);
MIKU_API int64_t miku_timer_next_deadline(const miku_timer_t *tm);
MIKU_API void  miku_timer_destroy(miku_timer_t *tm);

#endif
