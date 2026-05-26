#ifndef MIKU_SESSION_CACHE_H
#define MIKU_SESSION_CACHE_H

#include "miku_common.h"
#include "miku_redis.h"

typedef struct miku_session_cache_s miku_session_cache_t;

MIKU_API miku_session_cache_t *miku_session_cache_create(miku_redis_t *redis);
MIKU_API void                   miku_session_cache_destroy(miku_session_cache_t *cache);

MIKU_API int   miku_session_set_token(miku_session_cache_t *cache, const char *user_id,
                                        const char *token, int platform, int64_t ttl_ms);
MIKU_API int   miku_session_validate_token(miku_session_cache_t *cache, const char *user_id,
                                              const char *token);
MIKU_API int   miku_session_remove_token(miku_session_cache_t *cache, const char *user_id,
                                           int platform);
MIKU_API int   miku_session_remove_all(miku_session_cache_t *cache, const char *user_id);

MIKU_API int   miku_session_set_online(miku_session_cache_t *cache, const char *user_id,
                                         int platform, const char *conn_addr);
MIKU_API int   miku_session_set_offline(miku_session_cache_t *cache, const char *user_id,
                                          int platform);
MIKU_API int   miku_session_get_online(miku_session_cache_t *cache, const char *user_id,
                                          char **platforms_json);

#endif
