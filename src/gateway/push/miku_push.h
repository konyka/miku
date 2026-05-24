#ifndef MIKU_PUSH_H
#define MIKU_PUSH_H

#include "miku_common.h"

typedef struct miku_push_s miku_push_t;

MIKU_API miku_push_t *miku_push_create(void);
MIKU_API void miku_push_destroy(miku_push_t *p);
MIKU_API int miku_push_start(miku_push_t *p);
MIKU_API int miku_push_stop(miku_push_t *p);

#endif
