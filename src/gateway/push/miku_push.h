#ifndef MIKU_PUSH_H
#define MIKU_PUSH_H

#include "miku_common.h"

#define MK_PUSH_MAX_SUBS 4096

typedef void (*miku_push_send_fn)(const char *user_id, const char *payload, size_t len, void *ctx);

typedef struct miku_push_sub_s {
    char    user_id[64];
    int     platform;
    bool    active;
} miku_push_sub_t;

typedef struct miku_push_s miku_push_t;

MIKU_API miku_push_t *miku_push_create(void);
MIKU_API void miku_push_destroy(miku_push_t *p);
MIKU_API int  miku_push_start(miku_push_t *p);
MIKU_API int  miku_push_stop(miku_push_t *p);

MIKU_API int  miku_push_subscribe(miku_push_t *p, const char *user_id, int platform);
MIKU_API int  miku_push_unsubscribe(miku_push_t *p, const char *user_id);
MIKU_API int  miku_push_to_user(miku_push_t *p, const char *user_id,
                                 const char *title, const char *content);
MIKU_API int  miku_push_online_count(miku_push_t *p);

#endif
