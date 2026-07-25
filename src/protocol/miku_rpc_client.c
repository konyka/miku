#include "miku_rpc_client.h"
#include "miku_io.h"
#include "miku_token.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_string.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

static size_t rpc_read_full(int fd, void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t r = read(fd, (char *)buf + total, len - total);
        if (r > 0) { total += (size_t)r; continue; }
        if (r < 0 && errno == EINTR) continue;
        break;
    }
    return total;
}

static size_t rpc_write_full(int fd, const void *buf, size_t len) {
    size_t total = 0;
    while (total < len) {
        ssize_t w = miku_sock_write(fd, (const char *)buf + total, len - total);
        if (w > 0) { total += (size_t)w; continue; }
        if (w < 0 && errno == EINTR) continue;
        break;
    }
    return total;
}

int miku_rpc_json_add_internal_token(const char *payload_json,
                                      char *out, size_t out_cap) {
    if (!out || out_cap == 0) return -1;
    out[0] = '\0';
    miku_json_val_t *j = payload_json && payload_json[0]
        ? miku_json_parse_str(payload_json)
        : miku_json_create_object();
    if (!j) return -1;
    miku_jss(j, "internalToken", miku_internal_secret());
    miku_string_t *s = miku_json_stringify(j);
    miku_json_destroy(j);
    if (!s || !s->data) {
        miku_str_destroy(s);
        return -1;
    }
    if (s->len >= out_cap) {
        miku_str_destroy(s);
        return -1;
    }
    memcpy(out, s->data, s->len);
    out[s->len] = '\0';
    miku_str_destroy(s);
    return 0;
}

int miku_rpc_build_method_payload(const char *method, const miku_json_val_t *req,
                                  char *out, size_t out_cap) {
    if (!out || out_cap == 0 || !method || !method[0]) return -1;
    char em[128];
    miku_json_escape_str(method, em, sizeof(em));
    if (!req || miku_json_type(req) != MK_JSON_OBJECT || miku_json_size(req) == 0)
        return snprintf(out, out_cap, "{\"method\":\"%s\"}", em) >= (int)out_cap ? -1 : 0;

    miku_string_t *rs = miku_json_stringify(req);
    if (!rs || !rs->data || rs->data[0] != '{' || rs->len < 2) {
        miku_str_destroy(rs);
        return snprintf(out, out_cap, "{\"method\":\"%s\"}", em) >= (int)out_cap ? -1 : 0;
    }
    int n;
    if (rs->len == 2)
        n = snprintf(out, out_cap, "{\"method\":\"%s\"}", em);
    else
        n = snprintf(out, out_cap, "{\"method\":\"%s\",%.*s}",
                     em, (int)(rs->len - 2), rs->data + 1);
    miku_str_destroy(rs);
    return (n < 0 || (size_t)n >= out_cap) ? -1 : 0;
}

static int connect_host_port(const char *host, int port, int timeout_ms) {
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

        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        int rc = connect(fd, ai->ai_addr, ai->ai_addrlen);
        if (rc == 0) {
            if (flags >= 0) fcntl(fd, F_SETFL, flags);
            break;
        }
        if (errno != EINPROGRESS) {
            close(fd);
            fd = -1;
            continue;
        }
        struct pollfd pfd = { .fd = fd, .events = POLLOUT };
        rc = poll(&pfd, 1, timeout_ms);
        if (rc <= 0) {
            close(fd);
            fd = -1;
            continue;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl) != 0 || soerr != 0) {
            close(fd);
            fd = -1;
            continue;
        }
        if (flags >= 0) fcntl(fd, F_SETFL, flags);
        break;
    }
    freeaddrinfo(res);
    return fd;
}

int miku_rpc_call(const char *host, int port, const char *payload_json,
                  char *resp_body, size_t resp_cap, int with_internal_token) {
    if (!host || port <= 0 || !resp_body || resp_cap == 0) return -1;
    resp_body[0] = '\0';

    char payload_buf[65536];
    const char *payload = payload_json;
    if (with_internal_token) {
        if (miku_rpc_json_add_internal_token(payload_json, payload_buf, sizeof(payload_buf)) != 0)
            return -1;
        payload = payload_buf;
    }

    int fd = connect_host_port(host, port, 500);
    if (fd < 0) return -1;

    struct timeval io_tv = { .tv_sec = 5, .tv_usec = 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &io_tv, sizeof(io_tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &io_tv, sizeof(io_tv));

    uint32_t plen = (uint32_t)strlen(payload);
    uint8_t hdr[16] = {0};
    hdr[0] = 0x4D;
    hdr[1] = 0x4B;
    hdr[4] = 1;
    if (rpc_write_full(fd, hdr, 16) < 16) { close(fd); return -1; }

    uint8_t len_buf[4] = {
        (uint8_t)(plen >> 24), (uint8_t)(plen >> 16),
        (uint8_t)(plen >> 8),  (uint8_t)plen
    };
    if (rpc_write_full(fd, len_buf, 4) < 4) { close(fd); return -1; }
    if (rpc_write_full(fd, payload, plen) < plen) { close(fd); return -1; }

    uint8_t resp_len_buf[4];
    if (rpc_read_full(fd, resp_len_buf, 4) < 4) { close(fd); return -1; }
    uint32_t rlen = ((uint32_t)resp_len_buf[0] << 24) | ((uint32_t)resp_len_buf[1] << 16) |
                    ((uint32_t)resp_len_buf[2] << 8)  | (uint32_t)resp_len_buf[3];
    if (rlen == 0 || rlen >= resp_cap) { close(fd); return -1; }

    ssize_t total = 0;
    while (total < (ssize_t)rlen) {
        ssize_t n = read(fd, resp_body + total, (size_t)(rlen - (uint32_t)total));
        if (n <= 0) { close(fd); return -1; }
        total += n;
    }
    resp_body[rlen] = '\0';
    close(fd);
    return 0;
}
