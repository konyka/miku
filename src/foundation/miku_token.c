#include "miku_token.h"
#include "miku_hash.h"
#include "miku_uuid.h"
#include <stdio.h>
#include <string.h>
#include <time.h>

static uint64_t compute_sig(const char *uid, int platform, int64_t ts,
                            const char *nonce, const char *secret) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s:%d:%lld:%s:%s",
             uid, platform, (long long)ts, nonce, secret);
    return miku_fnv1a_64(buf, strlen(buf));
}

int miku_token_create(const char *user_id, int platform, const char *secret,
                       char *token_out, size_t token_cap) {
    if (!user_id || !user_id[0] || !secret || !token_out || token_cap < 32)
        return -1;

    char nonce[64];
    miku_uuid_generate(nonce);
    char nonce16[17] = {0};
    memcpy(nonce16, nonce, 16);

    int64_t ts = (int64_t)time(NULL);
    uint64_t sig = compute_sig(user_id, platform, ts, nonce16, secret);
    snprintf(token_out, token_cap, "miku|%s|%d|%lld|%s|%016llx",
             user_id, platform, (long long)ts, nonce16,
             (unsigned long long)sig);
    return 0;
}

int miku_token_verify(const char *token, const char *secret,
                       char *user_id_out, size_t cap) {
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

    int64_t now = (int64_t)time(NULL);
    if (now - ts > MIKU_TOKEN_EXPIRY_SECONDS) return -1;

    return 0;
}
