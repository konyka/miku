#ifndef MIKU_RATELIMIT_H
#define MIKU_RATELIMIT_H

#include "miku_common.h"

#define MK_RL_MAX_WINDOWS 4096

typedef struct miku_ratelimit_s miku_ratelimit_t;

MIKU_API miku_ratelimit_t *miku_ratelimit_create(int64_t window_ms, int max_requests);
MIKU_API void              miku_ratelimit_destroy(miku_ratelimit_t *rl);

MIKU_API int  miku_ratelimit_allow(miku_ratelimit_t *rl, const char *key);
MIKU_API int  miku_ratelimit_remaining(miku_ratelimit_t *rl, const char *key);
MIKU_API void miku_ratelimit_reset(miku_ratelimit_t *rl);
MIKU_API int64_t miku_ratelimit_window_ms(miku_ratelimit_t *rl);

#endif
