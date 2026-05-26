#ifndef MIKU_WEBHOOK_H
#define MIKU_WEBHOOK_H

#include "miku_common.h"

#define MK_WH_MAX_URLS 32
#define MK_WH_MAX_PAYLOAD 4096

typedef enum {
    MK_WH_BEFORE_SEND_MSG = 1,
    MK_WH_AFTER_SEND_MSG  = 2,
    MK_WH_BEFORE_ADD_FRIEND = 3,
    MK_WH_AFTER_ADD_FRIEND  = 4,
    MK_WH_BEFORE_CREATE_GROUP = 5,
    MK_WH_AFTER_CREATE_GROUP  = 6,
    MK_WH_BEFORE_JOIN_GROUP  = 7,
    MK_WH_AFTER_JOIN_GROUP   = 8,
    MK_WH_USER_ONLINE  = 9,
    MK_WH_USER_OFFLINE = 10,
    MK_WH_MSG_REVOKE   = 11,
} miku_webhook_event_t;

typedef struct miku_webhook_s miku_webhook_t;

typedef void (*miku_webhook_handler_fn)(miku_webhook_event_t event,
                                         const char *payload, void *ctx);

MIKU_API miku_webhook_t *miku_webhook_create(void);
MIKU_API void            miku_webhook_destroy(miku_webhook_t *wh);

MIKU_API int  miku_webhook_add_url(miku_webhook_t *wh, const char *url);
MIKU_API int  miku_webhook_remove_url(miku_webhook_t *wh, const char *url);
MIKU_API void miku_webhook_set_handler(miku_webhook_t *wh, miku_webhook_handler_fn fn, void *ctx);

MIKU_API int  miku_webhook_fire(miku_webhook_t *wh, miku_webhook_event_t event,
                                  const char *payload);
MIKU_API int  miku_webhook_fire_sync(miku_webhook_t *wh, miku_webhook_event_t event,
                                       const char *payload, char *resp, size_t resp_cap);

MIKU_API const char *miku_webhook_event_name(miku_webhook_event_t event);
#endif
