#include "miku_rpc_client.h"
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
#include <arpa/inet.h>
#include <poll.h>
#include <fcntl.h>
#include <errno.h>

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

    uint32_t plen = (uint32_t)strlen(payload);
    uint8_t hdr[16] = {0};
    hdr[0] = 0x4D;
    hdr[1] = 0x4B;
    hdr[4] = 1;
    if (write(fd, hdr, 16) != 16) { close(fd); return -1; }

    uint8_t len_buf[4] = {
        (uint8_t)(plen >> 24), (uint8_t)(plen >> 16),
        (uint8_t)(plen >> 8),  (uint8_t)plen
    };
    if (write(fd, len_buf, 4) != 4) { close(fd); return -1; }
    if (write(fd, payload, plen) != (ssize_t)plen) { close(fd); return -1; }

    uint8_t resp_len_buf[4];
    if (read(fd, resp_len_buf, 4) != 4) { close(fd); return -1; }
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
