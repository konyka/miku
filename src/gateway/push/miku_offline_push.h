#ifndef MIKU_OFFLINE_PUSH_H
#define MIKU_OFFLINE_PUSH_H

#include "miku_common.h"

typedef enum {
    MK_PUSH_PROVIDER_DUMMY = 0,
    MK_PUSH_PROVIDER_FCM   = 1,
    MK_PUSH_PROVIDER_GETUI = 2,
    MK_PUSH_PROVIDER_JPUSH = 3,
} miku_push_provider_t;

typedef struct miku_offline_push_s miku_offline_push_t;

MIKU_API miku_offline_push_t *miku_offline_push_create(miku_push_provider_t provider);
MIKU_API void                 miku_offline_push_destroy(miku_offline_push_t *op);

/* Optional HTTP gateway URL (http:// only). When set, non-DUMMY providers POST JSON. */
MIKU_API int  miku_offline_push_set_endpoint(miku_offline_push_t *op, const char *url);

MIKU_API int  miku_offline_push_send(miku_offline_push_t *op, const char *user_id,
                                       int platform, const char *title,
                                       const char *content, const char *client_url);
MIKU_API int  miku_offline_push_set_token(miku_offline_push_t *op, const char *user_id,
                                            int platform, const char *fcm_token);
MIKU_API int  miku_offline_push_del_token(miku_offline_push_t *op, const char *user_id,
                                            int platform);
MIKU_API const char *miku_offline_push_provider_name(miku_push_provider_t p);

#endif
