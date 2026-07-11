#include "miku_http_client.h"
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

int miku_http_post_json_resp(const char *url, const char *payload,
                             char *resp_body, size_t resp_cap) {
    if (resp_body && resp_cap > 0) resp_body[0] = '\0';

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
        "User-Agent: miku-http-client/0.1\r\n"
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
    if (status < 200 || status >= 300) return -1;

    if (resp_body && resp_cap > 0) {
        char *body = strstr(buf, "\r\n\r\n");
        if (body) {
            body += 4;
            strncpy(resp_body, body, resp_cap - 1);
            resp_body[resp_cap - 1] = '\0';
        }
    }
    return 0;
}

int miku_http_post_json(const char *url, const char *payload) {
    return miku_http_post_json_resp(url, payload, NULL, 0);
}
