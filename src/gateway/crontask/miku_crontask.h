#ifndef MIKU_CRONTASK_H
#define MIKU_CRONTASK_H

#include "miku_common.h"

typedef struct miku_crontask_s miku_crontask_t;

MIKU_API miku_crontask_t *miku_crontask_create(void);
MIKU_API void miku_crontask_destroy(miku_crontask_t *ct);
MIKU_API int miku_crontask_start(miku_crontask_t *ct);
MIKU_API int miku_crontask_stop(miku_crontask_t *ct);

#endif
