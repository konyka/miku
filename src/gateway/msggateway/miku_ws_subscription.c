#include "miku_ws_subscription.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    char subscriber[64];
    char target[64];
    bool active;
} sub_entry_t;

struct miku_ws_sub_s {
    sub_entry_t entries[MK_SUB_MAX];
    int         count;
};

miku_ws_sub_t *miku_ws_sub_create(void) {
    miku_ws_sub_t *s = (miku_ws_sub_t *)calloc(1, sizeof(*s));
    return s;
}

void miku_ws_sub_destroy(miku_ws_sub_t *sub) { free(sub); }

int miku_ws_sub_subscribe(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return -1;
    for (int i = 0; i < sub->count; i++) {
        if (sub->entries[i].active &&
            strcmp(sub->entries[i].subscriber, user_id) == 0 &&
            strcmp(sub->entries[i].target, target_user_id) == 0)
            return 0;
    }
    if (sub->count >= MK_SUB_MAX) return -1;
    sub_entry_t *e = &sub->entries[sub->count++];
    strncpy(e->subscriber, user_id, sizeof(e->subscriber) - 1);
    strncpy(e->target, target_user_id, sizeof(e->target) - 1);
    e->active = true;
    return 0;
}

int miku_ws_sub_unsubscribe(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return -1;
    for (int i = 0; i < sub->count; i++) {
        if (sub->entries[i].active &&
            strcmp(sub->entries[i].subscriber, user_id) == 0 &&
            strcmp(sub->entries[i].target, target_user_id) == 0) {
            sub->entries[i].active = false;
            return 0;
        }
    }
    return -1;
}

int miku_ws_sub_is_subscribed(miku_ws_sub_t *sub, const char *user_id, const char *target_user_id) {
    if (!sub || !user_id || !target_user_id) return 0;
    for (int i = 0; i < sub->count; i++) {
        if (sub->entries[i].active &&
            strcmp(sub->entries[i].subscriber, user_id) == 0 &&
            strcmp(sub->entries[i].target, target_user_id) == 0)
            return 1;
    }
    return 0;
}

int miku_ws_sub_get_subscribers(miku_ws_sub_t *sub, const char *target_user_id,
                                  char **user_ids, int max) {
    if (!sub || !target_user_id || !user_ids) return 0;
    int found = 0;
    for (int i = 0; i < sub->count && found < max; i++) {
        if (sub->entries[i].active &&
            strcmp(sub->entries[i].target, target_user_id) == 0) {
            user_ids[found++] = sub->entries[i].subscriber;
        }
    }
    return found;
}

void miku_ws_sub_user_online(miku_ws_sub_t *sub, const char *user_id, int platform) {
    (void)sub; (void)user_id; (void)platform;
}

void miku_ws_sub_user_offline(miku_ws_sub_t *sub, const char *user_id) {
    (void)sub; (void)user_id;
}
