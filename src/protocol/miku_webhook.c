#include "miku_webhook.h"
#include "miku_log.h"
#include "miku_threadpool.h"
#include "miku_atomic.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>

struct miku_webhook_s {
    char                   urls[MK_WH_MAX_URLS][512];
    int                    url_count;
    miku_webhook_handler_fn handler;
    void                  *handler_ctx;
    miku_threadpool_t     *pool;
    miku_atomic_int64_t    total_fired;
    miku_atomic_int64_t    total_success;
    miku_atomic_int64_t    total_failed;
};

typedef struct {
    miku_webhook_t *wh;
    char            url[512];
    char            body[MK_WH_MAX_PAYLOAD + 256];
} wh_job_t;

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
    miku_webhook_t *wh = (miku_webhook_t *)calloc(1, sizeof(miku_webhook_t));
    if (!wh) return NULL;
    wh->pool = miku_threadpool_create(2);
    return wh;
}

void miku_webhook_destroy(miku_webhook_t *wh) {
    if (!wh) return;
    if (wh->pool) {
        miku_threadpool_wait_idle(wh->pool);
        miku_threadpool_destroy(wh->pool);
    }
    free(wh);
}

void miku_webhook_wait_idle(miku_webhook_t *wh) {
    if (wh && wh->pool) miku_threadpool_wait_idle(wh->pool);
}

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

static int parse_http_url(const char *url, char *host, size_t host_cap,
                           int *port, char *path, size_t path_cap) {
    if (!url || strncmp(url, "http://", 7) != 0) return -1;
    const char *p = url + 7;
    const char *slash = strchr(p, '/');
    const char *colon = strchr(p, ':');
    size_t hlen;
    if (colon && (!slash || colon < slash)) {
        hlen = (size_t)(colon - p);
        *port = atoi(colon + 1);
    } else {
        hlen = slash ? (size_t)(slash - p) : strlen(p);
        *port = 80;
    }
    if (hlen == 0 || hlen >= host_cap) return -1;
    memcpy(host, p, hlen);
    host[hlen] = '\0';
    if (slash) {
        strncpy(path, slash, path_cap - 1);
        path[path_cap - 1] = '\0';
    } else {
        strncpy(path, "/", path_cap - 1);
    }
    return 0;
}

static int connect_timeout(int fd, const struct sockaddr *addr, socklen_t alen, int timeout_ms) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    int rc = connect(fd, addr, alen);
    if (rc == 0) {
        fcntl(fd, F_SETFL, flags);
        return 0;
    }
    if (errno != EINPROGRESS) return -1;

    struct pollfd pfd = { .fd = fd, .events = POLLOUT };
    rc = poll(&pfd, 1, timeout_ms);
    if (rc <= 0) return -1;
    int soerr = 0;
    socklen_t sl = sizeof(soerr);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0)
        return -1;
    fcntl(fd, F_SETFL, flags);
    return 0;
}

static int http_post_json(const char *url, const char *payload,
                           char *resp, size_t resp_cap) {
    char host[256], path[512];
    int port = 80;
    if (parse_http_url(url, host, sizeof(host), &port, path, sizeof(path)) != 0)
        return -1;

    char portstr[16];
    snprintf(portstr, sizeof(portstr), "%d", port);

    struct addrinfo hints = {0}, *res = NULL;
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host, portstr, &hints, &res) != 0 || !res) return -1;

    int fd = -1;
    for (struct addrinfo *ai = res; ai; ai = ai->ai_next) {
        fd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (fd < 0) continue;
        if (connect_timeout(fd, ai->ai_addr, ai->ai_addrlen, 200) == 0) break;
        close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) return -1;

    size_t body_len = payload ? strlen(payload) : 0;
    char req[8192];
    int n = snprintf(req, sizeof(req),
        "POST %s HTTP/1.1\r\n"
        "Host: %s:%d\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "User-Agent: miku-webhook/0.1\r\n"
        "\r\n"
        "%s",
        path, host, port, body_len, payload ? payload : "");
    if (n < 0 || (size_t)n >= sizeof(req)) {
        close(fd);
        return -1;
    }

    size_t sent = 0;
    while (sent < (size_t)n) {
        ssize_t w = write(fd, req + sent, (size_t)n - sent);
        if (w <= 0) { close(fd); return -1; }
        sent += (size_t)w;
    }

    char buf[4096];
    int total = 0;
    struct pollfd pfd = { .fd = fd, .events = POLLIN };
    while (total < (int)sizeof(buf) - 1) {
        if (poll(&pfd, 1, 200) <= 0) break;
        ssize_t r = read(fd, buf + total, sizeof(buf) - 1 - (size_t)total);
        if (r <= 0) break;
        total += (int)r;
    }
    buf[total] = '\0';
    close(fd);

    int status = 0;
    if (sscanf(buf, "HTTP/%*s %d", &status) != 1) return -1;

    if (resp && resp_cap > 0) {
        char *body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            strncpy(resp, body, resp_cap - 1);
            resp[resp_cap - 1] = '\0';
        } else {
            resp[0] = '\0';
        }
    }
    return (status >= 200 && status < 300) ? 0 : -1;
}

static void post_job(void *arg) {
    wh_job_t *job = (wh_job_t *)arg;
    if (!job || !job->wh) { free(job); return; }
    if (http_post_json(job->url, job->body, NULL, 0) == 0)
        miku_atomic_fetch_add(&job->wh->total_success, 1);
    else
        miku_atomic_fetch_add(&job->wh->total_failed, 1);
    free(job);
}

static void enqueue_url_posts(miku_webhook_t *wh, miku_webhook_event_t event,
                               const char *payload) {
    char envelope[MK_WH_MAX_PAYLOAD + 256];
    snprintf(envelope, sizeof(envelope),
             "{\"event\":\"%s\",\"data\":%s}",
             miku_webhook_event_name(event),
             payload && payload[0] ? payload : "{}");

    for (int i = 0; i < wh->url_count; i++) {
        if (wh->urls[i][0] == '\0') continue;
        if (!wh->pool) {
            if (http_post_json(wh->urls[i], envelope, NULL, 0) == 0)
                miku_atomic_fetch_add(&wh->total_success, 1);
            else
                miku_atomic_fetch_add(&wh->total_failed, 1);
            continue;
        }
        wh_job_t *job = (wh_job_t *)calloc(1, sizeof(*job));
        if (!job) {
            miku_atomic_fetch_add(&wh->total_failed, 1);
            continue;
        }
        job->wh = wh;
        strncpy(job->url, wh->urls[i], sizeof(job->url) - 1);
        strncpy(job->body, envelope, sizeof(job->body) - 1);
        if (miku_threadpool_submit(wh->pool, post_job, job) != 0) {
            free(job);
            miku_atomic_fetch_add(&wh->total_failed, 1);
        }
    }
}

static void post_all_urls_sync(miku_webhook_t *wh, miku_webhook_event_t event,
                                const char *payload, char *resp, size_t resp_cap) {
    char envelope[MK_WH_MAX_PAYLOAD + 256];
    snprintf(envelope, sizeof(envelope),
             "{\"event\":\"%s\",\"data\":%s}",
             miku_webhook_event_name(event),
             payload && payload[0] ? payload : "{}");

    for (int i = 0; i < wh->url_count; i++) {
        if (wh->urls[i][0] == '\0') continue;
        char *rbuf = (i == 0) ? resp : NULL;
        size_t rcap = (i == 0) ? resp_cap : 0;
        if (http_post_json(wh->urls[i], envelope, rbuf, rcap) == 0)
            miku_atomic_fetch_add(&wh->total_success, 1);
        else
            miku_atomic_fetch_add(&wh->total_failed, 1);
    }
}

int miku_webhook_fire(miku_webhook_t *wh, miku_webhook_event_t event, const char *payload) {
    if (!wh) return -1;
    miku_atomic_fetch_add(&wh->total_fired, 1);

    if (wh->handler) {
        wh->handler(event, payload, wh->handler_ctx);
    }

    if (wh->url_count > 0) {
        MK_LOG_DEBUG("webhook: fire %s → %d urls (async)", miku_webhook_event_name(event), wh->url_count);
        enqueue_url_posts(wh, event, payload);
    }
    return 0;
}

int miku_webhook_fire_sync(miku_webhook_t *wh, miku_webhook_event_t event,
                             const char *payload, char *resp, size_t resp_cap) {
    if (!wh) return -1;
    miku_atomic_fetch_add(&wh->total_fired, 1);

    if (wh->handler) {
        wh->handler(event, payload, wh->handler_ctx);
    }

    if (resp && resp_cap > 0) resp[0] = '\0';

    if (wh->url_count > 0) {
        MK_LOG_DEBUG("webhook: fire_sync %s", miku_webhook_event_name(event));
        post_all_urls_sync(wh, event, payload, resp, resp_cap);
    }
    return 0;
}

const char *miku_webhook_event_name(miku_webhook_event_t event) {
    int idx = (int)event;
    if (idx >= 0 && idx < (int)(sizeof(event_names) / sizeof(event_names[0])))
        return event_names[idx];
    return "unknown";
}
