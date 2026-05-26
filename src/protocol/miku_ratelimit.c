#include "miku_ratelimit.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char    key[128];
    int64_t    count;
    int64_t window_start;
} rl_entry_t;

struct miku_ratelimit_s {
    rl_entry_t entries[MK_RL_MAX_WINDOWS];
    int        entry_count;
    int64_t    window_ms;
    int        max_requests;
};

miku_ratelimit_t *miku_ratelimit_create(int64_t window_ms, int max_requests) {
    miku_ratelimit_t *rl = (miku_ratelimit_t *)calloc(1, sizeof(*rl));
    if (rl) { rl->window_ms = window_ms; rl->max_requests = max_requests; }
    return rl;
}

void miku_ratelimit_destroy(miku_ratelimit_t *rl) { free(rl); }

static rl_entry_t *find_entry(miku_ratelimit_t *rl, const char *key) {
    for (int i = 0; i < rl->entry_count; i++) {
        if (strcmp(rl->entries[i].key, key) == 0) return &rl->entries[i];
    }
    return NULL;
}

int miku_ratelimit_allow(miku_ratelimit_t *rl, const char *key) {
    if (!rl || !key) return 0;
    int64_t now = miku_timestamp_ms();
    rl_entry_t *e = find_entry(rl, key);
    if (!e) {
        if (rl->entry_count >= MK_RL_MAX_WINDOWS) return 0;
        e = &rl->entries[rl->entry_count++];
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->window_start = now;
        e->count = 0;
    }
    if (now - e->window_start >= rl->window_ms) {
        e->window_start = now;
        e->count = 0;
    }
    e->count++;
    return (e->count <= rl->max_requests) ? 1 : 0;
}

int miku_ratelimit_remaining(miku_ratelimit_t *rl, const char *key) {
    if (!rl || !key) return 0;
    rl_entry_t *e = find_entry(rl, key);
    if (!e) return rl->max_requests;
    int64_t now = miku_timestamp_ms();
    if (now - e->window_start >= rl->window_ms) return rl->max_requests;
    int rem = rl->max_requests - (int)e->count;
    return rem > 0 ? rem : 0;
}

void miku_ratelimit_reset(miku_ratelimit_t *rl) {
    if (rl) rl->entry_count = 0;
}

int64_t miku_ratelimit_window_ms(miku_ratelimit_t *rl) {
    return rl ? rl->window_ms : 0;
}
