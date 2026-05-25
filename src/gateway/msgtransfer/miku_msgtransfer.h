#ifndef MIKU_MSGTRANSFER_H
#define MIKU_MSGTRANSFER_H

#include "miku_common.h"
#include "miku_models.h"

#define MK_MT_QUEUE_SIZE 16384

typedef struct miku_msgtransfer_s miku_msgtransfer_t;

MIKU_API miku_msgtransfer_t *miku_msgtransfer_create(void);
MIKU_API void miku_msgtransfer_destroy(miku_msgtransfer_t *mt);
MIKU_API int  miku_msgtransfer_start(miku_msgtransfer_t *mt);
MIKU_API int  miku_msgtransfer_stop(miku_msgtransfer_t *mt);

MIKU_API int  miku_msgtransfer_enqueue(miku_msgtransfer_t *mt, const miku_msg_t *msg);
MIKU_API int  miku_msgtransfer_dequeue(miku_msgtransfer_t *mt, miku_msg_t *out);
MIKU_API int  miku_msgtransfer_pending(miku_msgtransfer_t *mt);
MIKU_API int64_t miku_msgtransfer_total_processed(miku_msgtransfer_t *mt);

#endif
