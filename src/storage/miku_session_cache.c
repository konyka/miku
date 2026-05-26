#include "miku_session_cache.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct miku_session_cache_s {
    miku_redis_t *redis;
    int           enabled;
};

static const char *token_prefix = "miku:token:";
static const char *online_prefix = "miku:online:";

miku_session_cache_t *miku_session_cache_create(miku_redis_t *redis) {
    miku_session_cache_t *c = (miku_session_cache_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->redis = redis;
    c->enabled = (redis != NULL);
    return c;
}

void miku_session_cache_destroy(miku_session_cache_t *cache) {
    free(cache);
}

int miku_session_set_token(miku_session_cache_t *cache, const char *user_id,
                             const char *token, int platform, int64_t ttl_ms) {
    if (!cache || !user_id || !token) return -1;
    if (!cache->enabled) {
        MK_LOG_DEBUG("session_cache: set_token stub (user=%s)", user_id);
        return 0;
    }

    char key[256];
    snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, platform);

    char val[512];
    snprintf(val, sizeof(val), "{\"token\":\"%s\",\"platform\":%d}", token, platform);

    int rc = miku_redis_set(cache->redis, key, val, ttl_ms);
    return rc;
}

int miku_session_validate_token(miku_session_cache_t *cache, const char *user_id,
                                  const char *token) {
    if (!cache || !user_id || !token) return -1;
    if (!cache->enabled) return 0;

    for (int plat = 1; plat <= 8; plat++) {
        char key[256];
        snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, plat);
        char *val = NULL;
        size_t vlen = 0;
        if (miku_redis_get(cache->redis, key, &val, &vlen) == 0 && val) {
            if (strstr(val, token)) {
                free(val);
                return 0;
            }
            free(val);
        }
    }
    return -1;
}

int miku_session_remove_token(miku_session_cache_t *cache, const char *user_id, int platform) {
    if (!cache || !user_id) return -1;
    if (!cache->enabled) return 0;

    char key[256];
    snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, platform);
    return miku_redis_del(cache->redis, key);
}

int miku_session_remove_all(miku_session_cache_t *cache, const char *user_id) {
    if (!cache || !user_id) return -1;
    if (!cache->enabled) return 0;

    for (int plat = 1; plat <= 8; plat++) {
        char key[256];
        snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, plat);
        miku_redis_del(cache->redis, key);
    }
    return 0;
}

int miku_session_set_online(miku_session_cache_t *cache, const char *user_id,
                              int platform, const char *conn_addr) {
    if (!cache || !user_id) return -1;
    if (!cache->enabled) return 0;

    char key[256];
    snprintf(key, sizeof(key), "%s%s", online_prefix, user_id);

    char field[32], val[128];
    snprintf(field, sizeof(field), "%d", platform);
    snprintf(val, sizeof(val), "%s:%ld", conn_addr ? conn_addr : "unknown", (long)miku_timestamp_ms());

    return miku_redis_hset(cache->redis, key, field, val);
}

int miku_session_set_offline(miku_session_cache_t *cache, const char *user_id, int platform) {
    if (!cache || !user_id) return -1;
    if (!cache->enabled) return 0;

    char key[256];
    snprintf(key, sizeof(key), "%s%s", online_prefix, user_id);
    char field[32];
    snprintf(field, sizeof(field), "%d", platform);
    return miku_redis_hdel(cache->redis, key, field);
}

int miku_session_get_online(miku_session_cache_t *cache, const char *user_id,
                              char **platforms_json) {
    if (!cache || !user_id) return -1;
    if (!cache->enabled) {
        if (platforms_json) *platforms_json = strdup("[]");
        return 0;
    }
    (void)platforms_json;
    return 0;
}
