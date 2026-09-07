#include "miku_session_cache.h"
#include "miku_hash.h"
#include "miku_log.h"
#include "miku_json_util.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MK_SESS_CAP  4096
#define MK_SESS_HASH 8192

typedef struct {
    int     used;
    char    user_id[64];
    int     platform;
    char    token[256];
    int64_t expire_ms;
    int     online;
    char    conn_addr[128];
} sess_slot_t;

struct miku_session_cache_s {
    miku_redis_t    *redis;
    int              enabled;
    sess_slot_t     *slots;
    int             *free_stack;
    int              free_top;
    int             *pair_hash;
    int              evict_cursor;
    pthread_rwlock_t lock;
};

static const char *token_prefix = "miku:token:";
static const char *online_prefix = "miku:online:";

static uint32_t pair_bucket(const char *user_id, int platform) {
    uint64_t a = miku_fnv1a_64(user_id, strlen(user_id));
    uint64_t b = (uint64_t)(unsigned)platform * 0x9e3779b97f4a7c15ULL;
    return (uint32_t)((a ^ b) & (MK_SESS_HASH - 1));
}

static void pair_hash_rebuild(miku_session_cache_t *c) {
    for (int i = 0; i < MK_SESS_HASH; i++) c->pair_hash[i] = -1;
    for (int slot = 0; slot < MK_SESS_CAP; slot++) {
        if (!c->slots[slot].used) continue;
        uint32_t h = pair_bucket(c->slots[slot].user_id, c->slots[slot].platform);
        for (int n = 0; n < MK_SESS_HASH; n++) {
            if (c->pair_hash[h] < 0) {
                c->pair_hash[h] = slot;
                break;
            }
            h = (h + 1) & (MK_SESS_HASH - 1);
        }
    }
}

static int pair_find(miku_session_cache_t *c, const char *user_id, int platform) {
    uint32_t h = pair_bucket(user_id, platform);
    for (int n = 0; n < MK_SESS_HASH; n++) {
        int slot = c->pair_hash[h];
        if (slot < 0) return -1;
        if (c->slots[slot].used &&
            c->slots[slot].platform == platform &&
            strcmp(c->slots[slot].user_id, user_id) == 0)
            return slot;
        h = (h + 1) & (MK_SESS_HASH - 1);
    }
    return -1;
}

static int alloc_slot(miku_session_cache_t *c) {
    int slot;
    if (c->free_top > 0) {
        slot = c->free_stack[--c->free_top];
    } else {
        slot = c->evict_cursor;
        c->evict_cursor = (c->evict_cursor + 1) % MK_SESS_CAP;
        if (c->slots[slot].used) {
            c->slots[slot].used = 0;
            if (c->free_top < MK_SESS_CAP) c->free_stack[c->free_top++] = slot;
            pair_hash_rebuild(c);
        }
        slot = c->free_stack[--c->free_top];
    }
    memset(&c->slots[slot], 0, sizeof(c->slots[slot]));
    c->slots[slot].used = 1;
    return slot;
}

static sess_slot_t *slot_for(miku_session_cache_t *c, const char *user_id, int platform) {
    int idx = pair_find(c, user_id, platform);
    if (idx < 0) {
        idx = alloc_slot(c);
        strncpy(c->slots[idx].user_id, user_id, sizeof(c->slots[idx].user_id) - 1);
        c->slots[idx].platform = platform;
        pair_hash_rebuild(c);
    }
    return &c->slots[idx];
}

miku_session_cache_t *miku_session_cache_create(miku_redis_t *redis) {
    miku_session_cache_t *c = (miku_session_cache_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    if (pthread_rwlock_init(&c->lock, NULL) != 0) {
        free(c);
        return NULL;
    }
    c->redis = redis;
    c->enabled = (redis != NULL);
    c->slots = (sess_slot_t *)calloc(MK_SESS_CAP, sizeof(sess_slot_t));
    c->free_stack = (int *)malloc(MK_SESS_CAP * sizeof(int));
    c->pair_hash = (int *)malloc(MK_SESS_HASH * sizeof(int));
    if (!c->slots || !c->free_stack || !c->pair_hash) {
        free(c->slots);
        free(c->free_stack);
        free(c->pair_hash);
        pthread_rwlock_destroy(&c->lock);
        free(c);
        return NULL;
    }
    for (int i = 0; i < MK_SESS_HASH; i++) c->pair_hash[i] = -1;
    for (int i = MK_SESS_CAP - 1; i >= 0; i--)
        c->free_stack[c->free_top++] = i;
    return c;
}

void miku_session_cache_destroy(miku_session_cache_t *cache) {
    if (!cache) return;
    free(cache->slots);
    free(cache->free_stack);
    free(cache->pair_hash);
    pthread_rwlock_destroy(&cache->lock);
    free(cache);
}

int miku_session_set_token(miku_session_cache_t *cache, const char *user_id,
                             const char *token, int platform, int64_t ttl_ms) {
    if (!cache || !user_id || !user_id[0] || !token) return -1;
    pthread_rwlock_wrlock(&cache->lock);
    sess_slot_t *s = slot_for(cache, user_id, platform);
    strncpy(s->token, token, sizeof(s->token) - 1);
    s->expire_ms = ttl_ms > 0 ? miku_timestamp_ms() + ttl_ms : 0;
    pthread_rwlock_unlock(&cache->lock);

    if (!cache->enabled) return 0;
    char key[256];
    snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, platform);
    char val[512];
    char etok[256];
    miku_json_escape_str(token, etok, sizeof(etok));
    snprintf(val, sizeof(val), "{\"token\":\"%s\",\"platform\":%d}", etok, platform);
    return miku_redis_set(cache->redis, key, val, ttl_ms);
}

int miku_session_validate_token(miku_session_cache_t *cache, const char *user_id,
                                  const char *token) {
    if (!cache || !user_id || !token) return -1;
    int64_t now = miku_timestamp_ms();
    pthread_rwlock_rdlock(&cache->lock);
    int ok = 0;
    for (int i = 0; i < MK_SESS_CAP; i++) {
        if (!cache->slots[i].used) continue;
        if (strcmp(cache->slots[i].user_id, user_id) != 0) continue;
        if (cache->slots[i].expire_ms > 0 && cache->slots[i].expire_ms < now) continue;
        if (strcmp(cache->slots[i].token, token) == 0) { ok = 1; break; }
    }
    pthread_rwlock_unlock(&cache->lock);
    if (ok) return 0;
    if (!cache->enabled) return -1;

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
    pthread_rwlock_wrlock(&cache->lock);
    int idx = pair_find(cache, user_id, platform);
    if (idx >= 0) {
        cache->slots[idx].used = 0;
        if (cache->free_top < MK_SESS_CAP) cache->free_stack[cache->free_top++] = idx;
        pair_hash_rebuild(cache);
    }
    pthread_rwlock_unlock(&cache->lock);
    if (!cache->enabled) return 0;
    char key[256];
    snprintf(key, sizeof(key), "%s%s:%d", token_prefix, user_id, platform);
    return miku_redis_del(cache->redis, key);
}

int miku_session_remove_all(miku_session_cache_t *cache, const char *user_id) {
    if (!cache || !user_id) return -1;
    pthread_rwlock_wrlock(&cache->lock);
    int changed = 0;
    for (int i = 0; i < MK_SESS_CAP; i++) {
        if (!cache->slots[i].used) continue;
        if (strcmp(cache->slots[i].user_id, user_id) != 0) continue;
        cache->slots[i].used = 0;
        if (cache->free_top < MK_SESS_CAP) cache->free_stack[cache->free_top++] = i;
        changed = 1;
    }
    if (changed) pair_hash_rebuild(cache);
    pthread_rwlock_unlock(&cache->lock);
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
    if (!cache || !user_id || !user_id[0]) return -1;
    pthread_rwlock_wrlock(&cache->lock);
    sess_slot_t *s = slot_for(cache, user_id, platform);
    s->online = 1;
    if (conn_addr)
        strncpy(s->conn_addr, conn_addr, sizeof(s->conn_addr) - 1);
    pthread_rwlock_unlock(&cache->lock);
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
    pthread_rwlock_wrlock(&cache->lock);
    int idx = pair_find(cache, user_id, platform);
    if (idx >= 0) cache->slots[idx].online = 0;
    pthread_rwlock_unlock(&cache->lock);
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
    char buf[512];
    size_t n = 0;
    buf[n++] = '[';
    pthread_rwlock_rdlock(&cache->lock);
    int first = 1;
    for (int i = 0; i < MK_SESS_CAP && n + 16 < sizeof(buf); i++) {
        if (!cache->slots[i].used || !cache->slots[i].online) continue;
        if (strcmp(cache->slots[i].user_id, user_id) != 0) continue;
        if (!first) buf[n++] = ',';
        first = 0;
        n += (size_t)snprintf(buf + n, sizeof(buf) - n, "%d", cache->slots[i].platform);
    }
    pthread_rwlock_unlock(&cache->lock);
    if (n < sizeof(buf) - 1) buf[n++] = ']';
    buf[n] = '\0';
    if (platforms_json) {
        *platforms_json = strdup(buf);
        if (!*platforms_json) return -1;
    }
    return 0;
}
