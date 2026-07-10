#include "miku_ratelimit.h"
#include "miku_thread.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char    key[128];
    int64_t count;
    int64_t window_start;
} rl_entry_t;

struct miku_ratelimit_s {
    rl_entry_t entries[MK_RL_MAX_WINDOWS];
    int        entry_count;
    int64_t    window_ms;
    int        max_requests;
    miku_mutex_t lock;
};

miku_ratelimit_t *miku_ratelimit_create(int64_t window_ms, int max_requests) {
    miku_ratelimit_t *rl = (miku_ratelimit_t *)calloc(1, sizeof(*rl));
    if (rl) {
        rl->window_ms = window_ms;
        rl->max_requests = max_requests;
        miku_mutex_init(&rl->lock);
    }
    return rl;
}

void miku_ratelimit_destroy(miku_ratelimit_t *rl) {
    if (!rl) return;
    miku_mutex_destroy(&rl->lock);
    free(rl);
}

static rl_entry_t *find_entry(miku_ratelimit_t *rl, const char *key) {
    for (int i = 0; i < rl->entry_count; i++) {
        if (strcmp(rl->entries[i].key, key) == 0) return &rl->entries[i];
    }
    return NULL;
}

/* Evict the oldest window when the table is full (LRU by window_start). */
static rl_entry_t *evict_oldest(miku_ratelimit_t *rl) {
    if (rl->entry_count <= 0) return NULL;
    int oldest = 0;
    for (int i = 1; i < rl->entry_count; i++) {
        if (rl->entries[i].window_start < rl->entries[oldest].window_start)
            oldest = i;
    }
    return &rl->entries[oldest];
}

int miku_ratelimit_allow(miku_ratelimit_t *rl, const char *key) {
    if (!rl || !key) return 0;
    int64_t now = miku_timestamp_ms();
    miku_mutex_lock(&rl->lock);

    rl_entry_t *e = find_entry(rl, key);
    if (!e) {
        if (rl->entry_count >= MK_RL_MAX_WINDOWS) {
            e = evict_oldest(rl);
        } else {
            e = &rl->entries[rl->entry_count++];
        }
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        e->window_start = now;
        e->count = 0;
    }
    if (now - e->window_start >= rl->window_ms) {
        e->window_start = now;
        e->count = 0;
    }
    e->count++;
    int allow = (e->count <= rl->max_requests) ? 1 : 0;
    miku_mutex_unlock(&rl->lock);
    return allow;
}

int miku_ratelimit_remaining(miku_ratelimit_t *rl, const char *key) {
    if (!rl || !key) return 0;
    miku_mutex_lock(&rl->lock);
    rl_entry_t *e = find_entry(rl, key);
    int rem;
    if (!e) {
        rem = rl->max_requests;
    } else {
        int64_t now = miku_timestamp_ms();
        if (now - e->window_start >= rl->window_ms) rem = rl->max_requests;
        else {
            rem = rl->max_requests - (int)e->count;
            if (rem < 0) rem = 0;
        }
    }
    miku_mutex_unlock(&rl->lock);
    return rem;
}

void miku_ratelimit_reset(miku_ratelimit_t *rl) {
    if (!rl) return;
    miku_mutex_lock(&rl->lock);
    rl->entry_count = 0;
    miku_mutex_unlock(&rl->lock);
}

int64_t miku_ratelimit_window_ms(miku_ratelimit_t *rl) {
    return rl ? rl->window_ms : 0;
}
