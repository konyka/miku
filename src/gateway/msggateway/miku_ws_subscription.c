#include "miku_ws_subscription.h"
#include "miku_hash.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define MK_SUB_HASH 8192

typedef struct {
    char subscriber[64];
    char target[64];
    bool active;
} sub_entry_t;

struct miku_ws_sub_s {
    sub_entry_t           entries[MK_SUB_MAX];
    int                   count;
    int16_t               pair_hash[MK_SUB_HASH];   /* (sub,target) → entry idx */
    int16_t               target_hash[MK_SUB_HASH]; /* target → first active entry */
    int16_t               target_next[MK_SUB_MAX];  /* intrusive list by target */
    miku_ws_sub_notify_fn notify;
    void                 *notify_ctx;
};

static uint32_t pair_slot(const char *subscriber, const char *target) {
    uint64_t a = miku_fnv1a_64(subscriber, strlen(subscriber));
    uint64_t b = miku_fnv1a_64(target, strlen(target));
    return (uint32_t)((a ^ (b * 0x9e3779b97f4a7c15ULL)) & (MK_SUB_HASH - 1));
}

static uint32_t target_slot(const char *target) {
    return (uint32_t)(miku_fnv1a_64(target, strlen(target)) & (MK_SUB_HASH - 1));
}

static void pair_hash_insert(miku_ws_sub_t *sub, int ei) {
    uint32_t idx = pair_slot(sub->entries[ei].subscriber, sub->entries[ei].target);
    for (int n = 0; n < MK_SUB_HASH; n++) {
        if (sub->pair_hash[idx] < 0) {
            sub->pair_hash[idx] = (int16_t)ei;
            return;
        }
        idx = (idx + 1) & (MK_SUB_HASH - 1);
    }
}

static int pair_hash_find(miku_ws_sub_t *sub, const char *subscriber, const char *target) {
    uint32_t idx = pair_slot(subscriber, target);
    for (int n = 0; n < MK_SUB_HASH; n++) {
        int ei = sub->pair_hash[idx];
        if (ei < 0) return -1;
        if (strcmp(sub->entries[ei].subscriber, subscriber) == 0 &&
            strcmp(sub->entries[ei].target, target) == 0)
            return ei;
        idx = (idx + 1) & (MK_SUB_HASH - 1);
    }
    return -1;
}

/* Rebuild target→chain index. Unsubscribe is rare; notify walks the chain. */
static void rebuild_target_chains(miku_ws_sub_t *sub) {
    for (int i = 0; i < MK_SUB_HASH; i++) sub->target_hash[i] = -1;
    for (int i = 0; i < MK_SUB_MAX; i++) sub->target_next[i] = -1;
    for (int ei = 0; ei < sub->count; ei++) {
        if (!sub->entries[ei].active) continue;
        const char *target = sub->entries[ei].target;
        uint32_t idx = target_slot(target);
        for (int n = 0; n < MK_SUB_HASH; n++) {
            int head = sub->target_hash[idx];
            if (head < 0) {
                sub->target_hash[idx] = (int16_t)ei;
                sub->target_next[ei] = -1;
                break;
            }
            if (strcmp(sub->entries[head].target, target) == 0) {
                sub->target_next[ei] = (int16_t)head;
                sub->target_hash[idx] = (int16_t)ei;
                break;
            }
            idx = (idx + 1) & (MK_SUB_HASH - 1);
        }
    }
}

static int target_chain_head(miku_ws_sub_t *sub, const char *target) {
    uint32_t idx = target_slot(target);
    for (int n = 0; n < MK_SUB_HASH; n++) {
        int head = sub->target_hash[idx];
        if (head < 0) return -1;
        if (strcmp(sub->entries[head].target, target) == 0) return head;
        idx = (idx + 1) & (MK_SUB_HASH - 1);
    }
    return -1;
}

miku_ws_sub_t *miku_ws_sub_create(void) {
    miku_ws_sub_t *sub = (miku_ws_sub_t *)calloc(1, sizeof(*sub));
    if (sub) {
        for (int i = 0; i < MK_SUB_HASH; i++) {
            sub->pair_hash[i] = -1;
            sub->target_hash[i] = -1;
        }
        for (int i = 0; i < MK_SUB_MAX; i++) sub->target_next[i] = -1;
    }
    return sub;
}

void miku_ws_sub_destroy(miku_ws_sub_t *sub) { free(sub); }

void miku_ws_sub_set_notify(miku_ws_sub_t *sub, miku_ws_sub_notify_fn fn, void *ctx) {
    if (!sub) return;
    sub->notify = fn;
    sub->notify_ctx = ctx;
}

int miku_ws_sub_subscribe(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return -1;
    int ei = pair_hash_find(sub, user_id, target_user_id);
    if (ei >= 0) {
        if (!sub->entries[ei].active) {
            sub->entries[ei].active = true;
            rebuild_target_chains(sub);
        }
        return 0;
    }
    if (sub->count >= MK_SUB_MAX) return -1;
    ei = sub->count++;
    sub_entry_t *e = &sub->entries[ei];
    memset(e, 0, sizeof(*e));
    strncpy(e->subscriber, user_id, sizeof(e->subscriber) - 1);
    strncpy(e->target, target_user_id, sizeof(e->target) - 1);
    e->active = true;
    pair_hash_insert(sub, ei);
    /* Prepend into target chain (add-only; no open-addressing holes yet). */
    {
        uint32_t idx = target_slot(target_user_id);
        for (int n = 0; n < MK_SUB_HASH; n++) {
            int head = sub->target_hash[idx];
            if (head < 0) {
                sub->target_hash[idx] = (int16_t)ei;
                sub->target_next[ei] = -1;
                break;
            }
            if (strcmp(sub->entries[head].target, target_user_id) == 0) {
                sub->target_next[ei] = (int16_t)head;
                sub->target_hash[idx] = (int16_t)ei;
                break;
            }
            idx = (idx + 1) & (MK_SUB_HASH - 1);
        }
    }
    return 0;
}

int miku_ws_sub_unsubscribe(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return -1;
    int ei = pair_hash_find(sub, user_id, target_user_id);
    if (ei < 0 || !sub->entries[ei].active) return -1;
    sub->entries[ei].active = false;
    rebuild_target_chains(sub);
    return 0;
}

int miku_ws_sub_is_subscribed(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return 0;
    int ei = pair_hash_find(sub, user_id, target_user_id);
    return (ei >= 0 && sub->entries[ei].active) ? 1 : 0;
}

int miku_ws_sub_get_subscribers(miku_ws_sub_t *sub, const char *target_user_id,
                                  char **user_ids, int max) {
    if (!sub || !target_user_id || !user_ids) return 0;
    int found = 0;
    for (int ei = target_chain_head(sub, target_user_id); ei >= 0 && found < max;
         ei = sub->target_next[ei]) {
        if (sub->entries[ei].active)
            user_ids[found++] = sub->entries[ei].subscriber;
    }
    return found;
}

static void notify_subscribers(miku_ws_sub_t *sub, const char *target_user_id,
                                 const char *payload) {
    if (!sub || !sub->notify || !target_user_id || !payload) return;
    char *uids[64];
    int n = miku_ws_sub_get_subscribers(sub, target_user_id, uids, 64);
    size_t len = strlen(payload);
    for (int i = 0; i < n; i++)
        sub->notify(uids[i], payload, len, sub->notify_ctx);
}

void miku_ws_sub_user_online(miku_ws_sub_t *sub, const char *user_id, int platform) {
    if (!sub || !user_id) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"userID\":\"%s\",\"platform\":%d,\"status\":\"online\"}",
             user_id, platform);
    notify_subscribers(sub, user_id, payload);
}

void miku_ws_sub_user_offline(miku_ws_sub_t *sub, const char *user_id) {
    if (!sub || !user_id) return;
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"userID\":\"%s\",\"status\":\"offline\"}", user_id);
    notify_subscribers(sub, user_id, payload);
}
