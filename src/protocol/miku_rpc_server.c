#include "miku_rpc_server.h"
#include "miku_log.h"
#include "miku_json.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

miku_rpc_server_t *miku_rpc_server_create(void *svc, miku_rpc_dispatch_fn dispatch, int port) {
    miku_rpc_server_t *srv = (miku_rpc_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    srv->svc = svc;
    srv->dispatch = dispatch;
    srv->port = port;
    srv->listen_fd = -1;
    return srv;
}

void miku_rpc_server_destroy(miku_rpc_server_t *srv) {
    if (!srv) return;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    free(srv);
}

int miku_rpc_server_start(miku_rpc_server_t *srv) {
    if (!srv) return -1;

    srv->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (srv->listen_fd < 0) {
        MK_LOG_ERROR("RPC server socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    addr.sin_port = htons((uint16_t)srv->port);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        MK_LOG_ERROR("RPC server bind(:%d) failed: %s", srv->port, strerror(errno));
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return -1;
    }

    if (listen(srv->listen_fd, 64) < 0) {
        MK_LOG_ERROR("RPC server listen() failed: %s", strerror(errno));
        close(srv->listen_fd);
        srv->listen_fd = -1;
        return -1;
    }

    srv->running = 1;
    MK_LOG_INFO("RPC server listening on :%d", srv->port);
    return 0;
}

void miku_rpc_server_stop(miku_rpc_server_t *srv) {
    if (!srv) return;
    srv->running = 0;
}

int miku_rpc_server_poll(miku_rpc_server_t *srv, int timeout_ms) {
    if (!srv || !srv->running || srv->listen_fd < 0) return -1;

    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(srv->listen_fd, &fds);

    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int ret = select(srv->listen_fd + 1, &fds, NULL, NULL, &tv);
    if (ret <= 0) return ret;

    struct sockaddr_in cli;
    socklen_t cli_len = sizeof(cli);
    int fd = accept(srv->listen_fd, (struct sockaddr *)&cli, &cli_len);
    if (fd < 0) return -1;
    if (srv->stats) miku_stats_conn_open(srv->stats);

    uint8_t hdr_buf[16];
    ssize_t n = read(fd, hdr_buf, 16);
    if (n < 16) { close(fd); return -1; }

    miku_rpc_header_t hdr;
    miku_rpc_header_decode(&hdr, hdr_buf);

    uint8_t len_buf[4];
    n = read(fd, len_buf, 4);
    if (n < 4) { close(fd); return -1; }
    uint32_t payload_len = ((uint32_t)len_buf[0] << 24) | ((uint32_t)len_buf[1] << 16) |
                           ((uint32_t)len_buf[2] << 8)  | (uint32_t)len_buf[3];

    char *payload = NULL;
    if (payload_len > 0 && payload_len < 65536) {
        payload = (char *)malloc((size_t)payload_len + 1);
        ssize_t total = 0;
        while (total < (ssize_t)payload_len) {
            ssize_t r = read(fd, payload + total, (size_t)(payload_len - (uint32_t)total));
            if (r <= 0) break;
            total += r;
        }
        payload[total] = '\0';
    }

    miku_json_val_t *req = payload ? miku_json_parse_str(payload) : miku_json_create_object();
    miku_json_val_t *resp = miku_json_create_object();

    const char *method = "";
    if (hdr.service == 0) {
        miku_json_val_t *m = miku_json_get(req, "method");
        if (m) method = miku_json_str(m);
    }

    srv->dispatch(srv->svc, method, req, resp);
    if (srv->stats) miku_stats_request_inc(srv->stats);

    miku_string_t *resp_str = miku_json_stringify(resp);
    uint32_t resp_len = (uint32_t)resp_str->len;
    uint8_t resp_len_buf[4] = {
        (uint8_t)(resp_len >> 24), (uint8_t)(resp_len >> 16),
        (uint8_t)(resp_len >> 8),  (uint8_t)resp_len
    };
    write(fd, resp_len_buf, 4);
    write(fd, resp_str->data, resp_str->len);

    if (srv->stats) {
        miku_stats_bytes_sent(srv->stats, (int64_t)resp_str->len);
        miku_stats_conn_close(srv->stats);
    }

    miku_str_destroy(resp_str);
    miku_json_destroy(resp);
    miku_json_destroy(req);
    free(payload);
    close(fd);
    return 1;
}

void miku_rpc_server_set_stats(miku_rpc_server_t *srv, miku_stats_t *stats) {
    if (srv) srv->stats = stats;
}
