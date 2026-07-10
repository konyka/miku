#include "miku_msggateway.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include "miku_http.h"
#include "miku_token.h"
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
    miku_msggw_on_op_fn  on_op;
    void                *on_op_ctx;
    int64_t              total_msgs_in;
    int64_t              total_msgs_out;
    int64_t              global_seq;
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

void miku_msggw_on_opcode(miku_msggw_t *gw, miku_msggw_on_op_fn fn, void *ctx) {
    if (!gw) return;
    gw->on_op = fn;
    gw->on_op_ctx = ctx;
}

int miku_msggw_send_op(miku_msggw_t *gw, int client_idx, int opcode,
                         const char *payload, size_t len) {
    if (!gw || client_idx < 0 || client_idx >= gw->client_count) return -1;
    miku_msggw_client_t *c = &gw->clients[client_idx];
    if (!c->online || !c->upgraded) return -1;

    char buf[8192];
    int n = snprintf(buf, sizeof(buf), "{\"reqIdentifier\":%d,\"data\":%.*s}",
                     opcode, (int)len, payload ? payload : "{}");
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    miku_ws_send_text(c->fd, buf, (size_t)n);
    gw->total_msgs_out++;
    return 0;
}

int miku_msggw_broadcast_op(miku_msggw_t *gw, int opcode,
                              const char *payload, size_t len) {
    if (!gw) return -1;
    char buf[8192];
    int n = snprintf(buf, sizeof(buf), "{\"reqIdentifier\":%d,\"data\":%.*s}",
                     opcode, (int)len, payload ? payload : "{}");
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    int sent = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].upgraded) {
            miku_ws_send_text(gw->clients[i].fd, buf, (size_t)n);
            sent++;
        }
    }
    gw->total_msgs_out += sent;
    return sent;
}

int miku_msggw_get_seq(miku_msggw_t *gw, const char *conversation_id, int64_t *seq) {
    if (!gw || !seq) return -1;
    (void)conversation_id;
    *seq = ++gw->global_seq;
    return 0;
}

int miku_msggw_set_background(miku_msggw_t *gw, int client_idx, bool background) {
    if (!gw || client_idx < 0 || client_idx >= gw->client_count) return -1;
    gw->clients[client_idx].is_background = background;
    return 0;
}

static int do_ws_upgrade(int fd, char *user_id_out, size_t user_id_cap, int *platform_out) {
    char buf[8192] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n <= 0) return -1;
    buf[n] = '\0';

    char *key_hdr = strstr(buf, "Sec-WebSocket-Key: ");
    if (!key_hdr) key_hdr = strstr(buf, "sec-websocket-key: ");
    if (!key_hdr) return -1;
    key_hdr = strchr(key_hdr, ':');
    if (!key_hdr) return -1;
    key_hdr++;
    while (*key_hdr == ' ') key_hdr++;
    char *key_end = strchr(key_hdr, '\r');
    if (!key_end) return -1;
    char sec_key[128] = {0};
    size_t klen = (size_t)(key_end - key_hdr);
    if (klen >= sizeof(sec_key)) return -1;
    memcpy(sec_key, key_hdr, klen);

    /* Extract token from query (?token=) or header (token: / Authorization:) */
    char token_buf[512] = {0};
    const char *token = NULL;
    char *q = strstr(buf, "GET ");
    if (q) {
        char *tok_q = strstr(q, "token=");
        if (tok_q && tok_q < strchr(q, '\r')) {
            tok_q += 6;
            char *te = tok_q;
            while (*te && *te != ' ' && *te != '&' && *te != '\r') te++;
            size_t tlen = (size_t)(te - tok_q);
            if (tlen > 0 && tlen < sizeof(token_buf)) {
                memcpy(token_buf, tok_q, tlen);
                token = token_buf;
            }
        }
    }
    if (!token) {
        char *th = strstr(buf, "\ntoken: ");
        if (!th) th = strstr(buf, "\nToken: ");
        if (!th) th = strstr(buf, "\nAuthorization: ");
        if (!th) th = strstr(buf, "\nauthorization: ");
        if (th) {
            th = strchr(th + 1, ':');
            if (th) {
                th++;
                while (*th == ' ') th++;
                if (strncmp(th, "Bearer ", 7) == 0) th += 7;
                char *te = strchr(th, '\r');
                if (te) {
                    size_t tlen = (size_t)(te - th);
                    if (tlen > 0 && tlen < sizeof(token_buf)) {
                        memcpy(token_buf, th, tlen);
                        token = token_buf;
                    }
                }
            }
        }
    }

    if (!token || !token[0]) {
        const char *deny =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: 47\r\n\r\n"
            "{\"errCode\":401,\"errMsg\":\"token required\"}";
        write(fd, deny, strlen(deny));
        return -1;
    }

    char uid[64] = {0};
    int platform = 0;
    if (miku_token_verify_ex(token, MIKU_TOKEN_DEFAULT_SECRET, uid, sizeof(uid),
                              &platform, NULL) != 0) {
        const char *deny =
            "HTTP/1.1 401 Unauthorized\r\n"
            "Content-Type: application/json\r\n"
            "Connection: close\r\n"
            "Content-Length: 47\r\n\r\n"
            "{\"errCode\":401,\"errMsg\":\"invalid token\"}";
        write(fd, deny, strlen(deny));
        return -1;
    }

    char accept[64] = {0};
    if (miku_ws_handshake(sec_key, accept, sizeof(accept)) != 0) return -1;

    char resp[512];
    int rlen = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
    if (write(fd, resp, (size_t)rlen) < 0) return -1;

    if (user_id_out && user_id_cap > 0) {
        strncpy(user_id_out, uid, user_id_cap - 1);
        user_id_out[user_id_cap - 1] = '\0';
    }
    if (platform_out) *platform_out = platform;
    return 0;
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
                int64_t req_op = miku_json_int(miku_json_get(j, "reqIdentifier"));
                if (req_op > 0 && gw->on_op) {
                    gw->on_op(idx, (int)req_op,
                              (const char *)frame->payload, frame->payload_len,
                              gw->on_op_ctx);
                    miku_json_destroy(j);
                } else {
                    if (gw->on_msg) {
                        gw->on_msg(c->user_id, (const char *)frame->payload,
                                   frame->payload_len, gw->on_msg_ctx);
                    }
                    miku_json_destroy(j);
                }
            } else {
                if (gw->on_msg) {
                    gw->on_msg(c->user_id, (const char *)frame->payload,
                               frame->payload_len, gw->on_msg_ctx);
                }
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
            char uid[64] = {0};
            int platform = 0;
            if (do_ws_upgrade(fd, uid, sizeof(uid), &platform) != 0) {
                close(fd);
            } else {
                int idx = find_or_add_client(gw, fd);
                if (idx >= 0) {
                    gw->clients[idx].upgraded = true;
                    strncpy(gw->clients[idx].user_id, uid, sizeof(gw->clients[idx].user_id) - 1);
                    gw->clients[idx].platform = platform;
                } else {
                    close(fd);
                }
            }
        }
    }

    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online && gw->clients[i].fd >= 0 &&
            FD_ISSET(gw->clients[i].fd, &rfds)) {
            if (!gw->clients[i].upgraded) {
                char uid[64] = {0};
                int platform = 0;
                if (do_ws_upgrade(gw->clients[i].fd, uid, sizeof(uid), &platform) != 0) {
                    close(gw->clients[i].fd);
                    gw->clients[i].online = false;
                    gw->clients[i].fd = -1;
                } else {
                    gw->clients[i].upgraded = true;
                    strncpy(gw->clients[i].user_id, uid, sizeof(gw->clients[i].user_id) - 1);
                    gw->clients[i].platform = platform;
                }
            } else {
                read_client_frames(gw, i);
            }
        }
    }
    return ret;
}
