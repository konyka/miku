#include "miku_redis.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#ifdef MIKU_HAS_REDIS
#include <hiredis/hiredis.h>

struct miku_redis_s {
    redisContext     *ctx;
    char              host[128];
    int               port;
    char              password[128];
    int               db;
    bool              connected;
};

miku_redis_t *miku_redis_create(const char *host, int port, const char *password, int db) {
    miku_redis_t *r = (miku_redis_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    strncpy(r->host, host ? host : "127.0.0.1", sizeof(r->host) - 1);
    r->port = port > 0 ? port : 6379;
    if (password) strncpy(r->password, password, sizeof(r->password) - 1);
    r->db = db;
    return r;
}

void miku_redis_destroy(miku_redis_t *r) {
    if (!r) return;
    miku_redis_disconnect(r);
    free(r);
}

int miku_redis_connect(miku_redis_t *r) {
    if (!r || r->connected) return -1;
    struct timeval tv = {2, 0};
    r->ctx = redisConnectWithTimeout(r->host, r->port, tv);
    if (!r->ctx || r->ctx->err) {
        MK_LOG_ERROR("Redis connect failed: %s", r->ctx ? r->ctx->errstr : "alloc failed");
        if (r->ctx) redisFree(r->ctx);
        r->ctx = NULL;
        return -1;
    }
    if (r->password[0]) {
        redisReply *reply = (redisReply *)redisCommand(r->ctx, "AUTH %s", r->password);
        if (!reply || reply->type == REDIS_REPLY_ERROR) {
            MK_LOG_ERROR("Redis AUTH failed");
            freeReplyObject(reply);
            redisFree(r->ctx);
            r->ctx = NULL;
            return -1;
        }
        freeReplyObject(reply);
    }
    if (r->db > 0) {
        redisReply *reply = (redisReply *)redisCommand(r->ctx, "SELECT %d", r->db);
        if (reply) freeReplyObject(reply);
    }
    r->connected = true;
    MK_LOG_INFO("Redis connected: %s:%d (db=%d)", r->host, r->port, r->db);
    return 0;
}

void miku_redis_disconnect(miku_redis_t *r) {
    if (!r || !r->connected) return;
    if (r->ctx) redisFree(r->ctx);
    r->ctx = NULL;
    r->connected = false;
}

bool miku_redis_is_connected(const miku_redis_t *r) {
    return r ? r->connected : false;
}

int miku_redis_set(miku_redis_t *r, const char *key, const char *val, int64_t ttl_ms) {
    if (!r || !r->connected || !key || !val) return -1;
    redisReply *reply;
    if (ttl_ms > 0) {
        reply = (redisReply *)redisCommand(r->ctx, "SET %s %s PX %lld", key, val, (long long)ttl_ms);
    } else {
        reply = (redisReply *)redisCommand(r->ctx, "SET %s %s", key, val);
    }
    int rc = (reply && reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_get(miku_redis_t *r, const char *key, char **val, size_t *val_len) {
    if (!r || !r->connected || !key || !val) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "GET %s", key);
    if (!reply) return -1;
    if (reply->type == REDIS_REPLY_STRING) {
        *val = strdup(reply->str);
        if (val_len) *val_len = reply->len;
        freeReplyObject(reply);
        return 0;
    }
    freeReplyObject(reply);
    return -1;
}

int miku_redis_del(miku_redis_t *r, const char *key) {
    if (!r || !r->connected || !key) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "DEL %s", key);
    int rc = (reply && reply->type == REDIS_REPLY_INTEGER) ? (int)reply->integer : -1;
    if (reply) freeReplyObject(reply);
    return rc > 0 ? 0 : -1;
}

int miku_redis_exists(miku_redis_t *r, const char *key) {
    if (!r || !r->connected || !key) return 0;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "EXISTS %s", key);
    int rc = (reply && reply->type == REDIS_REPLY_INTEGER) ? (int)reply->integer : 0;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_incr(miku_redis_t *r, const char *key, int64_t *new_val) {
    if (!r || !r->connected || !key) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "INCR %s", key);
    if (reply && reply->type == REDIS_REPLY_INTEGER) {
        if (new_val) *new_val = reply->integer;
        freeReplyObject(reply);
        return 0;
    }
    if (reply) freeReplyObject(reply);
    return -1;
}

int miku_redis_expire(miku_redis_t *r, const char *key, int64_t ttl_ms) {
    if (!r || !r->connected || !key) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "PEXPIRE %s %lld", key, (long long)ttl_ms);
    int rc = (reply && reply->type == REDIS_REPLY_INTEGER) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_hset(miku_redis_t *r, const char *hash, const char *field, const char *val) {
    if (!r || !r->connected || !hash || !field || !val) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "HSET %s %s %s", hash, field, val);
    int rc = (reply && reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_hget(miku_redis_t *r, const char *hash, const char *field, char **val) {
    if (!r || !r->connected || !hash || !field || !val) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "HGET %s %s", hash, field);
    if (reply && reply->type == REDIS_REPLY_STRING) {
        *val = strdup(reply->str);
        freeReplyObject(reply);
        return 0;
    }
    if (reply) freeReplyObject(reply);
    return -1;
}

int miku_redis_hdel(miku_redis_t *r, const char *hash, const char *field) {
    if (!r || !r->connected || !hash || !field) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "HDEL %s %s", hash, field);
    int rc = (reply && reply->type == REDIS_REPLY_INTEGER) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_publish(miku_redis_t *r, const char *channel, const char *msg, size_t msg_len) {
    if (!r || !r->connected || !channel || !msg) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "PUBLISH %s %b", channel, msg, msg_len);
    int rc = (reply && reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

int miku_redis_subscribe(miku_redis_t *r, const char *channel) {
    if (!r || !r->connected || !channel) return -1;
    redisReply *reply = (redisReply *)redisCommand(r->ctx, "SUBSCRIBE %s", channel);
    int rc = (reply && reply->type != REDIS_REPLY_ERROR) ? 0 : -1;
    if (reply) freeReplyObject(reply);
    return rc;
}

#else

struct miku_redis_s {
    char host[128];
    int  port;
    int  db;
    bool connected;
};

miku_redis_t *miku_redis_create(const char *host, int port, const char *password, int db) {
    miku_redis_t *r = (miku_redis_t *)calloc(1, sizeof(*r));
    if (!r) return NULL;
    if (host) strncpy(r->host, host, sizeof(r->host) - 1);
    r->port = port;
    r->db = db;
    (void)password;
    return r;
}

void miku_redis_destroy(miku_redis_t *r) { free(r); }

int miku_redis_connect(miku_redis_t *r) { (void)r; return -1; }
void miku_redis_disconnect(miku_redis_t *r) { if (r) r->connected = false; }
bool miku_redis_is_connected(const miku_redis_t *r) { (void)r; return false; }

int miku_redis_set(miku_redis_t *r, const char *k, const char *v, int64_t t) {
    (void)r; (void)k; (void)v; (void)t; return -1;
}
int miku_redis_get(miku_redis_t *r, const char *k, char **v, size_t *l) {
    (void)r; (void)k; (void)v; (void)l; return -1;
}
int miku_redis_del(miku_redis_t *r, const char *k) { (void)r; (void)k; return -1; }
int miku_redis_exists(miku_redis_t *r, const char *k) { (void)r; (void)k; return 0; }
int miku_redis_incr(miku_redis_t *r, const char *k, int64_t *nv) {
    (void)r; (void)k; (void)nv; return -1;
}
int miku_redis_expire(miku_redis_t *r, const char *k, int64_t t) {
    (void)r; (void)k; (void)t; return -1;
}
int miku_redis_hset(miku_redis_t *r, const char *h, const char *f, const char *v) {
    (void)r; (void)h; (void)f; (void)v; return -1;
}
int miku_redis_hget(miku_redis_t *r, const char *h, const char *f, char **v) {
    (void)r; (void)h; (void)f; (void)v; return -1;
}
int miku_redis_hdel(miku_redis_t *r, const char *h, const char *f) {
    (void)r; (void)h; (void)f; return -1;
}
int miku_redis_publish(miku_redis_t *r, const char *ch, const char *m, size_t l) {
    (void)r; (void)ch; (void)m; (void)l; return -1;
}
int miku_redis_subscribe(miku_redis_t *r, const char *ch) {
    (void)r; (void)ch; return -1;
}

#endif
