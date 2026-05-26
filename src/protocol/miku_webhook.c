#include "miku_webhook.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

struct miku_webhook_s {
    char                   urls[MK_WH_MAX_URLS][512];
    int                    url_count;
    miku_webhook_handler_fn handler;
    void                  *handler_ctx;
    int64_t                total_fired;
    int64_t                total_success;
    int64_t                total_failed;
};

static const char *event_names[] = {
    "unknown",
    "beforeSendMsg", "afterSendMsg",
    "beforeAddFriend", "afterAddFriend",
    "beforeCreateGroup", "afterCreateGroup",
    "beforeJoinGroup", "afterJoinGroup",
    "userOnline", "userOffline",
    "msgRevoke",
};

miku_webhook_t *miku_webhook_create(void) {
    return (miku_webhook_t *)calloc(1, sizeof(miku_webhook_t));
}

void miku_webhook_destroy(miku_webhook_t *wh) { free(wh); }

int miku_webhook_add_url(miku_webhook_t *wh, const char *url) {
    if (!wh || !url) return -1;
    if (wh->url_count >= MK_WH_MAX_URLS) return -1;
    strncpy(wh->urls[wh->url_count++], url, sizeof(wh->urls[0]) - 1);
    return 0;
}

int miku_webhook_remove_url(miku_webhook_t *wh, const char *url) {
    if (!wh || !url) return -1;
    for (int i = 0; i < wh->url_count; i++) {
        if (strcmp(wh->urls[i], url) == 0) {
            wh->urls[i][0] = '\0';
            return 0;
        }
    }
    return -1;
}

void miku_webhook_set_handler(miku_webhook_t *wh, miku_webhook_handler_fn fn, void *ctx) {
    if (!wh) return;
    wh->handler = fn;
    wh->handler_ctx = ctx;
}

int miku_webhook_fire(miku_webhook_t *wh, miku_webhook_event_t event, const char *payload) {
    if (!wh) return -1;
    wh->total_fired++;

    if (wh->handler) {
        wh->handler(event, payload, wh->handler_ctx);
    }

    if (wh->url_count > 0) {
        MK_LOG_DEBUG("webhook: fire %s → %d urls", miku_webhook_event_name(event), wh->url_count);
        wh->total_success += wh->url_count;
    }
    return 0;
}

int miku_webhook_fire_sync(miku_webhook_t *wh, miku_webhook_event_t event,
                             const char *payload, char *resp, size_t resp_cap) {
    if (!wh) return -1;
    wh->total_fired++;

    if (wh->handler) {
        wh->handler(event, payload, wh->handler_ctx);
    }

    if (resp && resp_cap > 0) resp[0] = '\0';

    if (wh->url_count > 0) {
        MK_LOG_DEBUG("webhook: fire_sync %s", miku_webhook_event_name(event));
        wh->total_success++;
    }
    return 0;
}

const char *miku_webhook_event_name(miku_webhook_event_t event) {
    int idx = (int)event;
    if (idx >= 0 && idx < (int)(sizeof(event_names) / sizeof(event_names[0])))
        return event_names[idx];
    return "unknown";
}
