#include "miku_ratelimit.h"
#include "miku_hash.h"
#include "miku_thread.h"
#include <stdlib.h>
#include <string.h>

/* 2x max windows for open-addressing load factor ~0.5 */
#define MK_RL_HASH 8192

typedef struct {
    char    key[128];
    int64_t count;
    int64_t window_start;
} rl_entry_t;

struct miku_ratelimit_s {
    rl_entry_t entries[MK_RL_MAX_WINDOWS];
    int        entry_count;
    int16_t    key_hash[MK_RL_HASH]; /* -1 empty, else entries[] index */
    int64_t    window_ms;
    int        max_requests;
    miku_mutex_t lock;
};

static uint32_t rl_hash_slot(const char *key) {
    return (uint32_t)(miku_fnv1a_64(key, strlen(key)) & (MK_RL_HASH - 1));
}

static void hash_insert_ei(miku_ratelimit_t *rl, int ei) {
    uint32_t idx = rl_hash_slot(rl->entries[ei].key);
    for (int n = 0; n < MK_RL_HASH; n++) {
        if (rl->key_hash[idx] < 0) {
            rl->key_hash[idx] = (int16_t)ei;
            return;
        }
        idx = (idx + 1) & (MK_RL_HASH - 1);
    }
}

/* Remove key from open-addressing table and repair the probe cluster. */
static void hash_remove_key(miku_ratelimit_t *rl, const char *key) {
    uint32_t idx = rl_hash_slot(key);
    for (int n = 0; n < MK_RL_HASH; n++) {
        int ei = rl->key_hash[idx];
        if (ei < 0) return;
        if (strcmp(rl->entries[ei].key, key) == 0) {
            rl->key_hash[idx] = -1;
            uint32_t i = (idx + 1) & (MK_RL_HASH - 1);
            while (rl->key_hash[i] >= 0) {
                int e2 = rl->key_hash[i];
                rl->key_hash[i] = -1;
                hash_insert_ei(rl, e2);
                i = (i + 1) & (MK_RL_HASH - 1);
            }
            return;
        }
        idx = (idx + 1) & (MK_RL_HASH - 1);
    }
}

static rl_entry_t *find_entry(miku_ratelimit_t *rl, const char *key) {
    uint32_t idx = rl_hash_slot(key);
    for (int n = 0; n < MK_RL_HASH; n++) {
        int ei = rl->key_hash[idx];
        if (ei < 0) return NULL;
        if (strcmp(rl->entries[ei].key, key) == 0)
            return &rl->entries[ei];
        idx = (idx + 1) & (MK_RL_HASH - 1);
    }
    return NULL;
}

/* Evict the oldest window when the table is full (LRU by window_start). */
static int evict_oldest_idx(miku_ratelimit_t *rl) {
    if (rl->entry_count <= 0) return -1;
    int oldest = 0;
    for (int i = 1; i < rl->entry_count; i++) {
        if (rl->entries[i].window_start < rl->entries[oldest].window_start)
            oldest = i;
    }
    return oldest;
}

miku_ratelimit_t *miku_ratelimit_create(int64_t window_ms, int max_requests) {
    miku_ratelimit_t *rl = (miku_ratelimit_t *)calloc(1, sizeof(*rl));
    if (rl) {
        rl->window_ms = window_ms;
        rl->max_requests = max_requests;
        for (int i = 0; i < MK_RL_HASH; i++) rl->key_hash[i] = -1;
        miku_mutex_init(&rl->lock);
    }
    return rl;
}

void miku_ratelimit_destroy(miku_ratelimit_t *rl) {
    if (!rl) return;
    miku_mutex_destroy(&rl->lock);
    free(rl);
}

int miku_ratelimit_allow(miku_ratelimit_t *rl, const char *key) {
    if (!rl || !key) return 0;
    int64_t now = miku_timestamp_ms();
    miku_mutex_lock(&rl->lock);

    rl_entry_t *e = find_entry(rl, key);
    if (!e) {
        int ei;
        if (rl->entry_count >= MK_RL_MAX_WINDOWS) {
            ei = evict_oldest_idx(rl);
            hash_remove_key(rl, rl->entries[ei].key);
        } else {
            ei = rl->entry_count++;
        }
        e = &rl->entries[ei];
        strncpy(e->key, key, sizeof(e->key) - 1);
        e->key[sizeof(e->key) - 1] = '\0';
        e->window_start = now;
        e->count = 0;
        hash_insert_ei(rl, ei);
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
    for (int i = 0; i < MK_RL_HASH; i++) rl->key_hash[i] = -1;
    miku_mutex_unlock(&rl->lock);
}

int64_t miku_ratelimit_window_ms(miku_ratelimit_t *rl) {
    return rl ? rl->window_ms : 0;
}
