#ifndef MIKU_REDIS_H
#define MIKU_REDIS_H

#include "miku_common.h"

typedef struct miku_redis_s miku_redis_t;

MIKU_API miku_redis_t *miku_redis_create(const char *host, int port, const char *password, int db);
MIKU_API void          miku_redis_destroy(miku_redis_t *r);

MIKU_API int   miku_redis_connect(miku_redis_t *r);
MIKU_API void  miku_redis_disconnect(miku_redis_t *r);
MIKU_API bool  miku_redis_is_connected(const miku_redis_t *r);

MIKU_API int   miku_redis_set(miku_redis_t *r, const char *key, const char *val, int64_t ttl_ms);
MIKU_API int   miku_redis_get(miku_redis_t *r, const char *key, char **val, size_t *val_len);
MIKU_API int   miku_redis_del(miku_redis_t *r, const char *key);
MIKU_API int   miku_redis_exists(miku_redis_t *r, const char *key);
MIKU_API int   miku_redis_incr(miku_redis_t *r, const char *key, int64_t *new_val);
MIKU_API int   miku_redis_expire(miku_redis_t *r, const char *key, int64_t ttl_ms);

MIKU_API int   miku_redis_hset(miku_redis_t *r, const char *hash, const char *field, const char *val);
MIKU_API int   miku_redis_hget(miku_redis_t *r, const char *hash, const char *field, char **val);
MIKU_API int   miku_redis_hdel(miku_redis_t *r, const char *hash, const char *field);

MIKU_API int   miku_redis_publish(miku_redis_t *r, const char *channel, const char *msg, size_t msg_len);
MIKU_API int   miku_redis_subscribe(miku_redis_t *r, const char *channel);

#endif
