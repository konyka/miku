#ifndef MIKU_WS_SUBSCRIPTION_H
#define MIKU_WS_SUBSCRIPTION_H

#include "miku_common.h"

#define MK_SUB_MAX 4096

typedef struct miku_ws_sub_s miku_ws_sub_t;

typedef void (*miku_ws_sub_notify_fn)(const char *subscriber, const char *payload,
                                       size_t len, void *ctx);

MIKU_API miku_ws_sub_t *miku_ws_sub_create(void);
MIKU_API void           miku_ws_sub_destroy(miku_ws_sub_t *sub);
MIKU_API void           miku_ws_sub_set_notify(miku_ws_sub_t *sub,
                                                 miku_ws_sub_notify_fn fn, void *ctx);

MIKU_API int  miku_ws_sub_subscribe(miku_ws_sub_t *sub, const char *user_id,
                                      const char *target_user_id);
MIKU_API int  miku_ws_sub_unsubscribe(miku_ws_sub_t *sub, const char *user_id,
                                        const char *target_user_id);
MIKU_API int  miku_ws_sub_is_subscribed(miku_ws_sub_t *sub, const char *user_id,
                                          const char *target_user_id);
MIKU_API int  miku_ws_sub_get_subscribers(miku_ws_sub_t *sub, const char *target_user_id,
                                            char **user_ids, int max);
MIKU_API void miku_ws_sub_user_online(miku_ws_sub_t *sub, const char *user_id, int platform);
MIKU_API void miku_ws_sub_user_offline(miku_ws_sub_t *sub, const char *user_id);

#endif
