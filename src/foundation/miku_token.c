#include "miku_token.h"
#include "miku_hash.h"
#include "miku_uuid.h"
#include "miku_thread.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MK_TOKEN_MAX_REVOKES 4096
#define MK_TOKEN_REVOKE_HASH 8192

typedef struct {
    char    key[96];   /* "uid:plat" or "uid:*" */
    int64_t since;     /* tokens with ts <= since are invalid */
} revoke_entry_t;

static revoke_entry_t g_revokes[MK_TOKEN_MAX_REVOKES];
static int            g_revoke_count;
static int16_t        g_revoke_hash[MK_TOKEN_REVOKE_HASH]; /* -1 empty */
static miku_mutex_t   g_revoke_lock;
static int            g_revoke_inited;

static uint32_t revoke_slot(const char *key) {
    return (uint32_t)(miku_fnv1a_64(key, strlen(key)) & (MK_TOKEN_REVOKE_HASH - 1));
}

static void revoke_hash_insert(int ei) {
    uint32_t idx = revoke_slot(g_revokes[ei].key);
    for (int n = 0; n < MK_TOKEN_REVOKE_HASH; n++) {
        if (g_revoke_hash[idx] < 0) {
            g_revoke_hash[idx] = (int16_t)ei;
            return;
        }
        idx = (idx + 1) & (MK_TOKEN_REVOKE_HASH - 1);
    }
}

static void revoke_hash_rebuild(void) {
    for (int i = 0; i < MK_TOKEN_REVOKE_HASH; i++) g_revoke_hash[i] = -1;
    for (int i = 0; i < g_revoke_count; i++) revoke_hash_insert(i);
}

static int revoke_hash_find(const char *key) {
    uint32_t idx = revoke_slot(key);
    for (int n = 0; n < MK_TOKEN_REVOKE_HASH; n++) {
        int ei = g_revoke_hash[idx];
        if (ei < 0) return -1;
        if (strcmp(g_revokes[ei].key, key) == 0) return ei;
        idx = (idx + 1) & (MK_TOKEN_REVOKE_HASH - 1);
    }
    return -1;
}

static void revoke_ensure_init(void) {
    if (!g_revoke_inited) {
        miku_mutex_init(&g_revoke_lock);
        for (int i = 0; i < MK_TOKEN_REVOKE_HASH; i++) g_revoke_hash[i] = -1;
        g_revoke_inited = 1;
    }
}

static uint64_t compute_sig(const char *uid, int platform, int64_t ts,
                            const char *nonce, const char *secret) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%d:%lld:%s:%s",
             uid, platform, (long long)ts, nonce, secret);
    return miku_fnv1a_64(buf, strlen(buf));
}

static int is_revoked(const char *user_id, int platform, int64_t ts) {
    revoke_ensure_init();
    char key_plat[96], key_all[96];
    snprintf(key_plat, sizeof(key_plat), "%s:%d", user_id, platform);
    snprintf(key_all, sizeof(key_all), "%s:*", user_id);

    miku_mutex_lock(&g_revoke_lock);
    int revoked = 0;
    int ei = revoke_hash_find(key_plat);
    if (ei >= 0 && ts <= g_revokes[ei].since) revoked = 1;
    if (!revoked) {
        ei = revoke_hash_find(key_all);
        if (ei >= 0 && ts <= g_revokes[ei].since) revoked = 1;
    }
    miku_mutex_unlock(&g_revoke_lock);
    return revoked;
}

int miku_token_create(const char *user_id, int platform, const char *secret,
                       char *token_out, size_t token_cap) {
    if (!user_id || !user_id[0] || !secret || !token_out || token_cap < 32)
        return -1;

    char nonce[64];
    miku_uuid_generate(nonce);
    char nonce16[17] = {0};
    memcpy(nonce16, nonce, 16);

    int64_t ts = miku_timestamp_ms();
    uint64_t sig = compute_sig(user_id, platform, ts, nonce16, secret);
    snprintf(token_out, token_cap, "miku|%s|%d|%lld|%s|%016llx",
             user_id, platform, (long long)ts, nonce16,
             (unsigned long long)sig);
    return 0;
}

int miku_token_verify_ex(const char *token, const char *secret,
                          char *user_id_out, size_t cap,
                          int *platform_out, int64_t *issued_at_out) {
    if (!token || !secret || !user_id_out || !cap) return -1;
    if (strncmp(token, "miku|", 5) != 0) return -1;

    const char *p = token + 5;
    const char *sep1 = strchr(p, '|');
    if (!sep1) return -1;
    size_t uid_len = (size_t)(sep1 - p);
    if (uid_len == 0 || uid_len >= cap) return -1;
    memcpy(user_id_out, p, uid_len);
    user_id_out[uid_len] = '\0';

    int platform = 0;
    if (sscanf(sep1 + 1, "%d", &platform) != 1) return -1;

    const char *sep2 = strchr(sep1 + 1, '|');
    if (!sep2) return -1;
    int64_t ts = 0;
    if (sscanf(sep2 + 1, "%lld", (long long *)&ts) != 1) return -1;

    const char *sep3 = strchr(sep2 + 1, '|');
    if (!sep3) return -1;
    char nonce17[17] = {0};
    const char *sep4 = strchr(sep3 + 1, '|');
    if (!sep4) return -1;
    size_t nonce_len = (size_t)(sep4 - sep3 - 1);
    if (nonce_len == 0 || nonce_len > 16) return -1;
    memcpy(nonce17, sep3 + 1, nonce_len);

    unsigned long long sig_from_token = 0;
    if (sscanf(sep4 + 1, "%llx", &sig_from_token) != 1) return -1;

    uint64_t expected = compute_sig(user_id_out, platform, ts, nonce17, secret);
    if ((unsigned long long)expected != sig_from_token) return -1;

    int64_t now = miku_timestamp_ms();
    if (now - ts > (int64_t)MIKU_TOKEN_EXPIRY_SECONDS * 1000LL) return -1;

    if (is_revoked(user_id_out, platform, ts)) return -1;

    if (platform_out) *platform_out = platform;
    if (issued_at_out) *issued_at_out = ts;
    return 0;
}

int miku_token_verify(const char *token, const char *secret,
                       char *user_id_out, size_t cap) {
    return miku_token_verify_ex(token, secret, user_id_out, cap, NULL, NULL);
}

int miku_token_revoke(const char *user_id, int platform) {
    if (!user_id || !user_id[0]) return -1;
    revoke_ensure_init();

    char key[96];
    if (platform < 0)
        snprintf(key, sizeof(key), "%s:*", user_id);
    else
        snprintf(key, sizeof(key), "%s:%d", user_id, platform);

    int64_t now = miku_timestamp_ms();
    miku_mutex_lock(&g_revoke_lock);
    int ei = revoke_hash_find(key);
    if (ei >= 0) {
        g_revokes[ei].since = now;
        miku_mutex_unlock(&g_revoke_lock);
        return 0;
    }
    if (g_revoke_count >= MK_TOKEN_MAX_REVOKES) {
        /* Evict oldest entry and rebuild hash (rare). */
        int oldest = 0;
        for (int i = 1; i < g_revoke_count; i++) {
            if (g_revokes[i].since < g_revokes[oldest].since) oldest = i;
        }
        strncpy(g_revokes[oldest].key, key, sizeof(g_revokes[oldest].key) - 1);
        g_revokes[oldest].key[sizeof(g_revokes[oldest].key) - 1] = '\0';
        g_revokes[oldest].since = now;
        revoke_hash_rebuild();
    } else {
        ei = g_revoke_count++;
        strncpy(g_revokes[ei].key, key, sizeof(g_revokes[0].key) - 1);
        g_revokes[ei].key[sizeof(g_revokes[0].key) - 1] = '\0';
        g_revokes[ei].since = now;
        revoke_hash_insert(ei);
    }
    miku_mutex_unlock(&g_revoke_lock);
    return 0;
}

void miku_token_revoke_clear(void) {
    revoke_ensure_init();
    miku_mutex_lock(&g_revoke_lock);
    g_revoke_count = 0;
    for (int i = 0; i < MK_TOKEN_REVOKE_HASH; i++) g_revoke_hash[i] = -1;
    miku_mutex_unlock(&g_revoke_lock);
}
