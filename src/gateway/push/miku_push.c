#include "miku_push.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include <stdlib.h>
#include <string.h>

struct miku_push_s {
    miku_push_sub_t subs[MK_PUSH_MAX_SUBS];
    int             sub_count;
    int             running;
    int64_t         push_count;
};

miku_push_t *miku_push_create(void) {
    return (miku_push_t *)calloc(1, sizeof(miku_push_t));
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
    for (int i = 0; i < p->sub_count; i++) {
        if (strcmp(p->subs[i].user_id, user_id) == 0) {
            p->subs[i].platform = platform;
            p->subs[i].active = true;
            return 0;
        }
    }
    if (p->sub_count >= MK_PUSH_MAX_SUBS) return -1;
    miku_push_sub_t *s = &p->subs[p->sub_count++];
    strncpy(s->user_id, user_id, sizeof(s->user_id) - 1);
    s->platform = platform;
    s->active = true;
    return 0;
}

int miku_push_unsubscribe(miku_push_t *p, const char *user_id) {
    if (!p || !user_id) return -1;
    for (int i = 0; i < p->sub_count; i++) {
        if (strcmp(p->subs[i].user_id, user_id) == 0) {
            p->subs[i].active = false;
            return 0;
        }
    }
    return -1;
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
    if (!p) return 0;
    int n = 0;
    for (int i = 0; i < p->sub_count; i++)
        if (p->subs[i].active) n++;
    return n;
}
