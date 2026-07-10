#ifndef MIKU_TOKEN_H
#define MIKU_TOKEN_H

#include "miku_common.h"

#define MIKU_TOKEN_DEFAULT_SECRET "openIM123"
#define MIKU_TOKEN_EXPIRY_SECONDS 86400

/* Create signed token: miku|<uid>|<platform>|<ts>|<nonce>|<sig>
 * Returns 0 on success, -1 on error. */
MIKU_API int miku_token_create(const char *user_id, int platform, const char *secret,
                                char *token_out, size_t token_cap);

/* Verify signed token. On success writes user_id into user_id_out and returns 0.
 * Returns -1 if missing/malformed/bad signature/expired. */
MIKU_API int miku_token_verify(const char *token, const char *secret,
                                char *user_id_out, size_t cap);

#endif
