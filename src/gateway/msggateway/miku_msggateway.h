#ifndef MIKU_MSGGATEWAY_H
#define MIKU_MSGGATEWAY_H

#include "miku_common.h"
#include "miku_websocket.h"

#define MK_GW_MAX_CLIENTS 4096

typedef struct miku_msggw_client_s {
    int            fd;
    char           user_id[64];
    int            platform;
    int64_t        connect_time;
    bool           online;
} miku_msggw_client_t;

typedef struct miku_msggw_s miku_msggw_t;

MIKU_API miku_msggw_t *miku_msggw_create(int port);
MIKU_API void miku_msggw_destroy(miku_msggw_t *gw);
MIKU_API int  miku_msggw_start(miku_msggw_t *gw);
MIKU_API int  miku_msggw_stop(miku_msggw_t *gw);

MIKU_API int  miku_msggw_client_count(miku_msggw_t *gw);
MIKU_API int  miku_msggw_broadcast(miku_msggw_t *gw, const char *msg, size_t len);
MIKU_API int  miku_msggw_send_to_user(miku_msggw_t *gw, const char *user_id,
                                       const char *msg, size_t len);
MIKU_API int  miku_msggw_kick_user(miku_msggw_t *gw, const char *user_id);

#endif
