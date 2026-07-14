#include "miku_push.h"
#include "miku_hash.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include <stdlib.h>
#include <string.h>

/* 2x max subs for open-addressing load factor ~0.5 */
#define MK_PUSH_HASH 8192

struct miku_push_s {
    miku_push_sub_t subs[MK_PUSH_MAX_SUBS];
    int             sub_count;
    int16_t         user_hash[MK_PUSH_HASH]; /* -1 empty, else subs[] index */
    int             online_count;
    int             running;
    int64_t         push_count;
};

static uint32_t push_hash_slot(const char *user_id) {
    return (uint32_t)(miku_fnv1a_64(user_id, strlen(user_id)) & (MK_PUSH_HASH - 1));
}

static void push_hash_insert(miku_push_t *p, int si) {
    uint32_t idx = push_hash_slot(p->subs[si].user_id);
    for (int n = 0; n < MK_PUSH_HASH; n++) {
        if (p->user_hash[idx] < 0) {
            p->user_hash[idx] = (int16_t)si;
            return;
        }
        idx = (idx + 1) & (MK_PUSH_HASH - 1);
    }
}

static int push_hash_find(miku_push_t *p, const char *user_id) {
    uint32_t idx = push_hash_slot(user_id);
    for (int n = 0; n < MK_PUSH_HASH; n++) {
        int si = p->user_hash[idx];
        if (si < 0) return -1;
        if (strcmp(p->subs[si].user_id, user_id) == 0) return si;
        idx = (idx + 1) & (MK_PUSH_HASH - 1);
    }
    return -1;
}

miku_push_t *miku_push_create(void) {
    miku_push_t *p = (miku_push_t *)calloc(1, sizeof(*p));
    if (p) {
        for (int i = 0; i < MK_PUSH_HASH; i++) p->user_hash[i] = -1;
    }
    return p;
}
void miku_push_destroy(miku_push_t *p) { free(p); }

int miku_push_start(miku_push_t *p) {
    if (!p) return -1;
    p->running = 1;
    MK_LOG_INFO("Push: started (max subs: %d)", MK_PUSH_MAX_SUBS);
    return 0;
}
int miku_push_stop(miku_push_t *p) {
    if (!p) return -1;
    p->running = 0;
    MK_LOG_INFO("Push: stopped (total pushed: %ld)", (long)p->push_count);
    return 0;
}

int miku_push_subscribe(miku_push_t *p, const char *user_id, int platform) {
    if (!p || !user_id) return -1;
    int si = push_hash_find(p, user_id);
    if (si >= 0) {
        if (!p->subs[si].active) {
            p->subs[si].active = true;
            p->online_count++;
        }
        p->subs[si].platform = platform;
        return 0;
    }
    if (p->sub_count >= MK_PUSH_MAX_SUBS) return -1;
    si = p->sub_count++;
    miku_push_sub_t *s = &p->subs[si];
    memset(s, 0, sizeof(*s));
    strncpy(s->user_id, user_id, sizeof(s->user_id) - 1);
    s->platform = platform;
    s->active = true;
    p->online_count++;
    push_hash_insert(p, si);
    return 0;
}

int miku_push_unsubscribe(miku_push_t *p, const char *user_id) {
    if (!p || !user_id) return -1;
    int si = push_hash_find(p, user_id);
    if (si < 0) return -1;
    if (p->subs[si].active) {
        p->subs[si].active = false;
        if (p->online_count > 0) p->online_count--;
    }
    return 0;
}

int miku_push_to_user(miku_push_t *p, const char *user_id,
                       const char *title, const char *content) {
    if (!p || !user_id) return -1;
    miku_json_val_t *n = miku_json_create_object();
    miku_json_object_set(n, "type", miku_json_create_str("push"));
    miku_json_object_set(n, "userID", miku_json_create_str(user_id));
    if (title) miku_json_object_set(n, "title", miku_json_create_str(title));
    if (content) miku_json_object_set(n, "content", miku_json_create_str(content));
    miku_string_t *s = miku_json_stringify(n);
    MK_LOG_INFO("Push -> %s: %s", user_id, title ? title : "");
    miku_str_destroy(s);
    miku_json_destroy(n);
    p->push_count++;
    return 0;
}

int miku_push_online_count(miku_push_t *p) {
    return p ? p->online_count : 0;
}
