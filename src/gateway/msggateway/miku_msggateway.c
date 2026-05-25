#include "miku_msggateway.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include "miku_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

struct miku_msggw_s {
    int                  port;
    int                  listen_fd;
    int                  running;
    miku_msggw_client_t  clients[MK_GW_MAX_CLIENTS];
    int                  client_count;
    miku_msggw_on_msg_fn on_msg;
    void                *on_msg_ctx;
    int64_t              total_msgs_in;
    int64_t              total_msgs_out;
};

miku_msggw_t *miku_msggw_create(int port) {
    miku_msggw_t *gw = (miku_msggw_t *)calloc(1, sizeof(*gw));
    if (gw) gw->port = port;
    return gw;
}

void miku_msggw_destroy(miku_msggw_t *gw) { free(gw); }

int miku_msggw_start(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (gw->listen_fd < 0) return -1;
    int opt = 1;
    setsockopt(gw->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)gw->port);
    if (bind(gw->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(gw->listen_fd, 128) < 0) {
        close(gw->listen_fd);
        return -1;
    }
    gw->running = 1;
    MK_LOG_INFO("MsgGateway: listening on :%d (ws)", gw->port);
    return 0;
}

int miku_msggw_stop(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->running = 0;
    if (gw->listen_fd >= 0) { close(gw->listen_fd); gw->listen_fd = -1; }
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].fd >= 0)
            close(gw->clients[i].fd);
    }
    gw->client_count = 0;
    MK_LOG_INFO("MsgGateway: stopped (msgs in:%ld out:%ld)",
                (long)gw->total_msgs_in, (long)gw->total_msgs_out);
    return 0;
}

int miku_msggw_client_count(miku_msggw_t *gw) {
    if (!gw) return 0;
    int n = 0;
    for (int i = 0; i < gw->client_count; i++)
        if (gw->clients[i].online) n++;
    return n;
}

int miku_msggw_broadcast(miku_msggw_t *gw, const char *msg, size_t len) {
    if (!gw || !msg) return -1;
    int sent = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].upgraded) {
            miku_ws_send_text(gw->clients[i].fd, msg, len);
            sent++;
        }
    }
    gw->total_msgs_out += sent;
    return sent;
}

int miku_msggw_send_to_user(miku_msggw_t *gw, const char *user_id,
                             const char *msg, size_t len) {
    if (!gw || !user_id || !msg) return -1;
    int sent = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].upgraded &&
            strcmp(gw->clients[i].user_id, user_id) == 0) {
            miku_ws_send_text(gw->clients[i].fd, msg, len);
            sent++;
        }
    }
    gw->total_msgs_out += sent;
    return sent;
}

int miku_msggw_kick_user(miku_msggw_t *gw, const char *user_id) {
    if (!gw || !user_id) return -1;
    int kicked = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && strcmp(gw->clients[i].user_id, user_id) == 0) {
            miku_ws_send_close(gw->clients[i].fd, 1000, "kicked");
            close(gw->clients[i].fd);
            gw->clients[i].online = false;
            gw->clients[i].fd = -1;
            kicked++;
        }
    }
    return kicked;
}

void miku_msggw_on_message(miku_msggw_t *gw, miku_msggw_on_msg_fn fn, void *ctx) {
    if (!gw) return;
    gw->on_msg = fn;
    gw->on_msg_ctx = ctx;
}

static void do_ws_upgrade(int fd) {
    char buf[4096] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return;
    buf[n] = '\0';

    char *key_hdr = strstr(buf, "Sec-WebSocket-Key: ");
    if (!key_hdr) return;
    key_hdr += 19;
    char *key_end = strchr(key_hdr, '\r');
    if (!key_end) return;
    char sec_key[128] = {0};
    size_t klen = (size_t)(key_end - key_hdr);
    if (klen >= sizeof(sec_key)) return;
    memcpy(sec_key, key_hdr, klen);

    char accept[64] = {0};
    if (miku_ws_handshake(sec_key, accept, sizeof(accept)) != 0) return;

    char resp[512];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    write(fd, resp, (size_t)rlen);
}

static int find_or_add_client(miku_msggw_t *gw, int fd) {
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].fd == fd && gw->clients[i].online) return i;
    }
    if (gw->client_count >= MK_GW_MAX_CLIENTS) return -1;
    int idx = gw->client_count++;
    memset(&gw->clients[idx], 0, sizeof(gw->clients[idx]));
    gw->clients[idx].fd = fd;
    gw->clients[idx].online = true;
    gw->clients[idx].connect_time = miku_timestamp_ms();
    return idx;
}

static void read_client_frames(miku_msggw_t *gw, int idx) {
    miku_msggw_client_t *c = &gw->clients[idx];
    miku_ws_frame_t *frame = miku_ws_frame_create();
    while (true) {
        int rc = miku_ws_frame_read(c->fd, frame);
        if (rc <= 0) break;
        if (frame->opcode == MK_WS_TEXT && frame->payload && frame->payload_len > 0) {
            gw->total_msgs_in++;
            miku_json_val_t *j = miku_json_parse_str((const char *)frame->payload);
            if (j) {
                const char *uid = miku_json_str(miku_json_get(j, "userID"));
                if (uid && c->user_id[0] == '\0') {
                    strncpy(c->user_id, uid, sizeof(c->user_id) - 1);
                }
                miku_json_destroy(j);
            }
            if (gw->on_msg) {
                gw->on_msg(c->user_id, (const char *)frame->payload,
                           frame->payload_len, gw->on_msg_ctx);
            }
        } else if (frame->opcode == MK_WS_PING) {
            miku_ws_send_pong(c->fd, frame->payload, frame->payload_len);
        } else if (frame->opcode == MK_WS_CLOSE) {
            c->online = false;
            close(c->fd);
            c->fd = -1;
            miku_ws_frame_destroy(frame);
            return;
        }
    }
    miku_ws_frame_destroy(frame);
}

int miku_msggw_poll(miku_msggw_t *gw, int timeout_ms) {
    if (!gw || !gw->running) return -1;

    fd_set rfds;
    FD_ZERO(&rfds);
    int maxfd = gw->listen_fd;
    FD_SET(gw->listen_fd, &rfds);

    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].fd >= 0) {
            FD_SET(gw->clients[i].fd, &rfds);
            if (gw->clients[i].fd > maxfd) maxfd = gw->clients[i].fd;
        }
    }

    struct timeval tv = {0, timeout_ms * 1000};
    int ret = select(maxfd + 1, &rfds, NULL, NULL, &tv);
    if (ret <= 0) return ret;

    if (FD_ISSET(gw->listen_fd, &rfds)) {
        int fd = accept(gw->listen_fd, NULL, NULL);
        if (fd >= 0) {
            do_ws_upgrade(fd);
            int idx = find_or_add_client(gw, fd);
            if (idx >= 0) gw->clients[idx].upgraded = true;
        }
    }

    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].fd >= 0 &&
            FD_ISSET(gw->clients[i].fd, &rfds)) {
            if (!gw->clients[i].upgraded) {
                do_ws_upgrade(gw->clients[i].fd);
                gw->clients[i].upgraded = true;
            } else {
                read_client_frames(gw, i);
            }
        }
    }
    return ret;
}
