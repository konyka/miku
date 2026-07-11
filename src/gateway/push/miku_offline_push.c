#include "miku_offline_push.h"
#include "miku_log.h"
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

#define MK_MAX_PUSH_TOKENS 4096

typedef struct {
    char user_id[64];
    int  platform;
    char token[512];
    bool active;
} push_token_t;

struct miku_offline_push_s {
    miku_push_provider_t provider;
    push_token_t         tokens[MK_MAX_PUSH_TOKENS];
    int                  token_count;
    int64_t              total_sent;
    int64_t              total_http_ok;
    int64_t              total_http_fail;
    char                 endpoint[512];
};

miku_offline_push_t *miku_offline_push_create(miku_push_provider_t provider) {
    miku_offline_push_t *op = (miku_offline_push_t *)calloc(1, sizeof(*op));
    if (op) op->provider = provider;
    return op;
}

void miku_offline_push_destroy(miku_offline_push_t *op) { free(op); }

int miku_offline_push_set_endpoint(miku_offline_push_t *op, const char *url) {
    if (!op || !url) return -1;
    if (strncmp(url, "http://", 7) != 0) {
        MK_LOG_WARN("offline_push: endpoint must be http:// (got %s)", url);
        return -1;
    }
    strncpy(op->endpoint, url, sizeof(op->endpoint) - 1);
    return 0;
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

static int http_post_json(const char *url, const char *payload) {
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
        "User-Agent: miku-offline-push/0.1\r\n"
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

    char buf[1024];
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
    return (status >= 200 && status < 300) ? 0 : -1;
}

int miku_offline_push_send(miku_offline_push_t *op, const char *user_id,
                             int platform, const char *title,
                             const char *content, const char *client_url) {
    if (!op || !user_id) return -1;
    op->total_sent++;

    if (op->provider == MK_PUSH_PROVIDER_DUMMY) {
        MK_LOG_DEBUG("offline_push[DUMMY]: user=%s platform=%d title=%s", user_id, platform, title);
        return 0;
    }

    const char *token = NULL;
    for (int i = 0; i < op->token_count; i++) {
        if (op->tokens[i].active &&
            strcmp(op->tokens[i].user_id, user_id) == 0 &&
            op->tokens[i].platform == platform) {
            token = op->tokens[i].token;
            break;
        }
    }
    if (!token) {
        MK_LOG_DEBUG("offline_push: no token for user=%s platform=%d", user_id, platform);
        return -1;
    }

    if (op->endpoint[0] == '\0') {
        /* Dry-run when no gateway configured — keeps unit tests offline. */
        MK_LOG_DEBUG("offline_push[%s]: dry-run user=%s token=%s... title=%s",
                     miku_offline_push_provider_name(op->provider),
                     user_id, token, title ? title : "");
        return 0;
    }

    char body[2048];
    snprintf(body, sizeof(body),
        "{\"provider\":\"%s\",\"userID\":\"%s\",\"platform\":%d,"
        "\"token\":\"%s\",\"title\":\"%s\",\"content\":\"%s\",\"ex\":\"%s\"}",
        miku_offline_push_provider_name(op->provider),
        user_id, platform, token,
        title ? title : "", content ? content : "",
        client_url ? client_url : "");

    int rc = http_post_json(op->endpoint, body);
    if (rc == 0) {
        op->total_http_ok++;
        MK_LOG_DEBUG("offline_push[%s]: HTTP OK user=%s",
                     miku_offline_push_provider_name(op->provider), user_id);
    } else {
        op->total_http_fail++;
        MK_LOG_WARN("offline_push[%s]: HTTP fail user=%s endpoint=%s",
                    miku_offline_push_provider_name(op->provider), user_id, op->endpoint);
    }
    return rc;
}

int miku_offline_push_set_token(miku_offline_push_t *op, const char *user_id,
                                  int platform, const char *fcm_token) {
    if (!op || !user_id || !fcm_token) return -1;
    for (int i = 0; i < op->token_count; i++) {
        if (strcmp(op->tokens[i].user_id, user_id) == 0 && op->tokens[i].platform == platform) {
            strncpy(op->tokens[i].token, fcm_token, sizeof(op->tokens[i].token) - 1);
            op->tokens[i].active = true;
            return 0;
        }
    }
    if (op->token_count >= MK_MAX_PUSH_TOKENS) return -1;
    push_token_t *t = &op->tokens[op->token_count++];
    strncpy(t->user_id, user_id, sizeof(t->user_id) - 1);
    t->platform = platform;
    strncpy(t->token, fcm_token, sizeof(t->token) - 1);
    t->active = true;
    return 0;
}

int miku_offline_push_del_token(miku_offline_push_t *op, const char *user_id, int platform) {
    if (!op || !user_id) return -1;
    for (int i = 0; i < op->token_count; i++) {
        if (strcmp(op->tokens[i].user_id, user_id) == 0 && op->tokens[i].platform == platform) {
            op->tokens[i].active = false;
            return 0;
        }
    }
    return -1;
}

const char *miku_offline_push_provider_name(miku_push_provider_t p) {
    switch (p) {
        case MK_PUSH_PROVIDER_FCM:   return "FCM";
        case MK_PUSH_PROVIDER_GETUI: return "Getui";
        case MK_PUSH_PROVIDER_JPUSH: return "JPUSH";
        default:                     return "DUMMY";
    }
}
