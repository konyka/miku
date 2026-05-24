#ifndef MIKU_MSGGATEWAY_H
#define MIKU_MSGGATEWAY_H

#include "miku_common.h"
#include "miku_websocket.h"

typedef struct miku_msggw_s miku_msggw_t;

MIKU_API miku_msggw_t *miku_msggw_create(int port);
MIKU_API void miku_msggw_destroy(miku_msggw_t *gw);
MIKU_API int miku_msggw_start(miku_msggw_t *gw);
MIKU_API int miku_msggw_stop(miku_msggw_t *gw);

#endif
