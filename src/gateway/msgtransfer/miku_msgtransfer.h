#ifndef MIKU_MSGTRANSFER_H
#define MIKU_MSGTRANSFER_H

#include "miku_common.h"

typedef struct miku_msgtransfer_s miku_msgtransfer_t;

MIKU_API miku_msgtransfer_t *miku_msgtransfer_create(void);
MIKU_API void miku_msgtransfer_destroy(miku_msgtransfer_t *mt);
MIKU_API int miku_msgtransfer_start(miku_msgtransfer_t *mt);
MIKU_API int miku_msgtransfer_stop(miku_msgtransfer_t *mt);

#endif
