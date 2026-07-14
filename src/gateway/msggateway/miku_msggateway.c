#include "miku_msggateway.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_string.h"
#include "miku_http.h"
#include "miku_token.h"
#include "miku_io.h"
#include "miku_hash.h"
#include "miku_seq.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>

#define MK_GW_FD_MAP   65536
#define MK_GW_USER_HASH 8192

struct miku_msggw_s {
    int                  port;
    int                  listen_fd;
    int                  running;
    int                  listen_registered;
    miku_io_t           *io;
    miku_msggw_client_t  clients[MK_GW_MAX_CLIENTS];
    int                  client_count;
    int16_t              fd_map[MK_GW_FD_MAP];   /* fd → client idx, -1 empty */
    int16_t              user_head[MK_GW_USER_HASH]; /* user hash → first idx */
    int16_t              user_next[MK_GW_MAX_CLIENTS]; /* sibling chain */
    int16_t              free_stack[MK_GW_MAX_CLIENTS]; /* offline slots for reuse */
    int                  free_top;
    int                  online_count;
    miku_msggw_on_msg_fn on_msg;
    void                *on_msg_ctx;
    miku_msggw_on_op_fn  on_op;
    void                *on_op_ctx;
    miku_msggw_on_presence_fn on_presence;
    void                *on_presence_ctx;
    int64_t              total_msgs_in;
    int64_t              total_msgs_out;
    miku_seq_t          *seq;
};

miku_msggw_t *miku_msggw_create(int port) {
    miku_msggw_t *gw = (miku_msggw_t *)calloc(1, sizeof(*gw));
    if (gw) {
        gw->port = port;
        gw->listen_fd = -1;
        gw->seq = miku_seq_create();
        if (!gw->seq) { free(gw); return NULL; }
        for (int i = 0; i < MK_GW_FD_MAP; i++) gw->fd_map[i] = -1;
        for (int i = 0; i < MK_GW_USER_HASH; i++) gw->user_head[i] = -1;
        for (int i = 0; i < MK_GW_MAX_CLIENTS; i++) gw->user_next[i] = -1;
    }
    return gw;
}

void miku_msggw_destroy(miku_msggw_t *gw) {
    if (!gw) return;
    if (gw->io) miku_io_destroy(gw->io);
    miku_seq_destroy(gw->seq);
    free(gw);
}

static uint32_t user_bucket(const char *user_id) {
    return (uint32_t)(miku_fnv1a_64(user_id, strlen(user_id)) & (MK_GW_USER_HASH - 1));
}

static void fd_map_set(miku_msggw_t *gw, int fd, int idx) {
    if (fd >= 0 && fd < MK_GW_FD_MAP) gw->fd_map[fd] = (int16_t)idx;
}

static void fd_map_clear(miku_msggw_t *gw, int fd) {
    if (fd >= 0 && fd < MK_GW_FD_MAP) gw->fd_map[fd] = -1;
}

static void user_index_add(miku_msggw_t *gw, int idx, const char *user_id) {
    if (!user_id || !user_id[0]) return;
    uint32_t b = user_bucket(user_id);
    gw->user_next[idx] = gw->user_head[b];
    gw->user_head[b] = (int16_t)idx;
}

static void user_index_del(miku_msggw_t *gw, int idx) {
    const char *uid = gw->clients[idx].user_id;
    if (!uid[0]) return;
    uint32_t b = user_bucket(uid);
    int16_t *p = &gw->user_head[b];
    while (*p >= 0) {
        if (*p == idx) {
            *p = gw->user_next[idx];
            gw->user_next[idx] = -1;
            return;
        }
        p = &gw->user_next[*p];
    }
}

static void client_offline(miku_msggw_t *gw, int idx) {
    if (idx < 0 || idx >= gw->client_count) return;
    miku_msggw_client_t *c = &gw->clients[idx];
    if (!c->online) return;
    if (c->user_id[0] && gw->on_presence)
        gw->on_presence(c->user_id, c->platform, 0, gw->on_presence_ctx);
    user_index_del(gw, idx);
    if (c->fd >= 0) {
        fd_map_clear(gw, c->fd);
        if (gw->io) miku_io_del(gw->io, c->fd);
        close(c->fd);
        c->fd = -1;
    }
    c->online = false;
    if (gw->online_count > 0) gw->online_count--;
    c->upgraded = false;
    c->user_id[0] = '\0';
    if (gw->free_top < MK_GW_MAX_CLIENTS)
        gw->free_stack[gw->free_top++] = (int16_t)idx;
}

static int find_client_by_fd(miku_msggw_t *gw, int fd) {
    if (fd >= 0 && fd < MK_GW_FD_MAP) {
        int idx = gw->fd_map[fd];
        if (idx >= 0 && idx < gw->client_count &&
            gw->clients[idx].fd == fd && gw->clients[idx].online)
            return idx;
    }
    /* Fallback for fds >= MK_GW_FD_MAP (rare) */
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].fd == fd && gw->clients[i].online) return i;
    }
    return -1;
}

int miku_msggw_start(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->io = miku_io_create();
    if (!gw->io) return -1;

    gw->listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (gw->listen_fd < 0) {
        miku_io_destroy(gw->io);
        gw->io = NULL;
        return -1;
    }
    int opt = 1;
    setsockopt(gw->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)gw->port);
    if (bind(gw->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 ||
        listen(gw->listen_fd, 128) < 0) {
        close(gw->listen_fd);
        gw->listen_fd = -1;
        miku_io_destroy(gw->io);
        gw->io = NULL;
        return -1;
    }
    /* Listen fd registered in poll via accept callback setup below */
    gw->running = 1;
    MK_LOG_INFO("MsgGateway: listening on :%d (ws, epoll)", gw->port);
    return 0;
}

int miku_msggw_stop(miku_msggw_t *gw) {
    if (!gw) return -1;
    gw->running = 0;
    for (int i = 0; i < gw->client_count; i++) {
        if (gw->clients[i].online) client_offline(gw, i);
    }
    gw->client_count = 0;
    gw->free_top = 0;
    gw->online_count = 0;
    for (int i = 0; i < MK_GW_FD_MAP; i++) gw->fd_map[i] = -1;
    for (int i = 0; i < MK_GW_USER_HASH; i++) gw->user_head[i] = -1;
    for (int i = 0; i < MK_GW_MAX_CLIENTS; i++) gw->user_next[i] = -1;
    if (gw->listen_fd >= 0) {
        if (gw->io) miku_io_del(gw->io, gw->listen_fd);
        close(gw->listen_fd);
        gw->listen_fd = -1;
    }
    gw->listen_registered = 0;
    if (gw->io) {
        miku_io_destroy(gw->io);
        gw->io = NULL;
    }
    MK_LOG_INFO("MsgGateway: stopped (msgs in:%ld out:%ld)",
                (long)gw->total_msgs_in, (long)gw->total_msgs_out);
    return 0;
}

int miku_msggw_client_count(miku_msggw_t *gw) {
    return gw ? gw->online_count : 0;
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
    uint32_t b = user_bucket(user_id);
    for (int idx = gw->user_head[b]; idx >= 0; idx = gw->user_next[idx]) {
        if (gw->clients[idx].online && gw->clients[idx].upgraded &&
            strcmp(gw->clients[idx].user_id, user_id) == 0) {
            miku_ws_send_text(gw->clients[idx].fd, msg, len);
            sent++;
        }
    }
    gw->total_msgs_out += sent;
    return sent;
}

int miku_msggw_send_op_to_user(miku_msggw_t *gw, const char *user_id,
                                 int opcode, const char *payload, size_t len) {
    if (!gw || !user_id) return -1;
    char buf[8192];
    int n = snprintf(buf, sizeof(buf), "{\"reqIdentifier\":%d,\"data\":%.*s}",
                     opcode, (int)len, payload ? payload : "{}");
    if (n < 0 || (size_t)n >= sizeof(buf)) return -1;
    return miku_msggw_send_to_user(gw, user_id, buf, (size_t)n);
}

int miku_msggw_kick_user(miku_msggw_t *gw, const char *user_id, int platform) {
    if (!gw || !user_id) return -1;
    static const char kick_payload[] = "{\"reason\":\"forced offline\"}";
    int kicked = 0;
    uint32_t b = user_bucket(user_id);
    int idx = gw->user_head[b];
    while (idx >= 0) {
        int next = gw->user_next[idx];
        if (gw->clients[idx].online &&
            strcmp(gw->clients[idx].user_id, user_id) == 0 &&
            (platform < 0 || gw->clients[idx].platform == platform)) {
            /* Notify SDK before closing so clients can handle force-logout cleanly. */
            miku_msggw_send_op(gw, idx, MK_WS_OP_KICK_ONLINE,
                               kick_payload, sizeof(kick_payload) - 1);
            miku_ws_send_close(gw->clients[idx].fd, 1000, "kicked");
            client_offline(gw, idx);
            kicked++;
        }
        idx = next;
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

void miku_msggw_on_presence(miku_msggw_t *gw, miku_msggw_on_presence_fn fn, void *ctx) {
    if (!gw) return;
    gw->on_presence = fn;
    gw->on_presence_ctx = ctx;
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

int miku_msggw_peek_max_seq(miku_msggw_t *gw, const char *conversation_id, int64_t *seq) {
    if (!gw || !seq || !gw->seq) return -1;
    const char *cid = (conversation_id && conversation_id[0]) ? conversation_id : "default";
    *seq = miku_seq_current(gw->seq, cid);
    return 0;
}

int miku_msggw_alloc_seq(miku_msggw_t *gw, const char *conversation_id, int64_t *seq) {
    if (!gw || !seq || !gw->seq) return -1;
    const char *cid = (conversation_id && conversation_id[0]) ? conversation_id : "default";
    int64_t n = miku_seq_next(gw->seq, cid);
    if (n < 0) return -1;
    *seq = n;
    return 0;
}

int miku_msggw_get_seq(miku_msggw_t *gw, const char *conversation_id, int64_t *seq) {
    return miku_msggw_alloc_seq(gw, conversation_id, seq);
}

int miku_msggw_set_user_read(miku_msggw_t *gw, const char *user_id,
                               const char *conversation_id, int64_t read_seq) {
    if (!gw || !gw->seq || !user_id || !conversation_id) return -1;
    const char *cid = conversation_id[0] ? conversation_id : "default";
    return miku_seq_set_user_read(gw->seq, user_id, cid, read_seq);
}

int64_t miku_msggw_get_user_read(miku_msggw_t *gw, const char *user_id,
                                   const char *conversation_id) {
    if (!gw || !gw->seq || !user_id || !conversation_id) return 0;
    const char *cid = conversation_id[0] ? conversation_id : "default";
    return miku_seq_get_user_read(gw->seq, user_id, cid);
}

int miku_msggw_set_background(miku_msggw_t *gw, int client_idx, bool background) {
    if (!gw || client_idx < 0 || client_idx >= gw->client_count) return -1;
    gw->clients[client_idx].is_background = background;
    return 0;
}

int miku_msggw_disconnect_client(miku_msggw_t *gw, int client_idx) {
    if (!gw || client_idx < 0 || client_idx >= gw->client_count) return -1;
    if (!gw->clients[client_idx].online) return 0;
    if (gw->clients[client_idx].upgraded)
        miku_ws_send_close(gw->clients[client_idx].fd, 1000, "logout");
    client_offline(gw, client_idx);
    return 0;
}

int miku_msggw_get_client_user_id(miku_msggw_t *gw, int client_idx,
                                    char *out, size_t out_cap) {
    if (!gw || !out || out_cap == 0 || client_idx < 0 || client_idx >= gw->client_count)
        return -1;
    if (!gw->clients[client_idx].online || !gw->clients[client_idx].user_id[0])
        return -1;
    strncpy(out, gw->clients[client_idx].user_id, out_cap - 1);
    out[out_cap - 1] = '\0';
    return 0;
}

int miku_msggw_unwrap_op_data(const char *envelope_json, int *out_opcode,
                                char **out_data, size_t *out_len) {
    if (!envelope_json || !out_opcode || !out_data) return -1;
    *out_data = NULL;
    if (out_len) *out_len = 0;
    miku_json_val_t *j = miku_json_parse_str(envelope_json);
    if (!j) return -1;
    int64_t op = miku_json_int(miku_json_get(j, "reqIdentifier"));
    if (op <= 0) { miku_json_destroy(j); return -1; }
    *out_opcode = (int)op;
    miku_json_val_t *data = miku_json_get(j, "data");
    if (data) {
        miku_string_t *s = miku_json_stringify(data);
        miku_json_destroy(j);
        if (!s || !s->data) { miku_str_destroy(s); return -1; }
        *out_data = s->data;
        if (out_len) *out_len = s->len;
        s->data = NULL; /* transfer ownership */
        miku_str_destroy(s);
        return 0;
    }
    miku_json_destroy(j);
    *out_data = strdup("{}");
    if (!*out_data) return -1;
    if (out_len) *out_len = 2;
    return 0;
}

static int do_ws_upgrade(int fd, char *user_id_out, size_t user_id_cap, int *platform_out) {
    char buf[8192] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    if (n < 0) return -1;          /* errno may be EAGAIN */
    if (n == 0) { errno = ECONNRESET; return -1; }
    buf[n] = '\0';
    errno = 0;

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
    int existing = find_client_by_fd(gw, fd);
    if (existing >= 0) return existing;

    int idx;
    if (gw->free_top > 0) {
        idx = gw->free_stack[--gw->free_top];
        memset(&gw->clients[idx], 0, sizeof(gw->clients[idx]));
        gw->user_next[idx] = -1;
    } else {
        if (gw->client_count >= MK_GW_MAX_CLIENTS) return -1;
        idx = gw->client_count++;
        memset(&gw->clients[idx], 0, sizeof(gw->clients[idx]));
        gw->user_next[idx] = -1;
    }
    gw->clients[idx].fd = fd;
    gw->clients[idx].online = true;
    gw->online_count++;
    gw->clients[idx].connect_time = miku_timestamp_ms();
    fd_map_set(gw, fd, idx);
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
                    miku_json_val_t *data = miku_json_get(j, "data");
                    if (data) {
                        miku_string_t *s = miku_json_stringify(data);
                        if (s && s->data)
                            gw->on_op(idx, (int)req_op, s->data, s->len, gw->on_op_ctx);
                        else
                            gw->on_op(idx, (int)req_op, "{}", 2, gw->on_op_ctx);
                        miku_str_destroy(s);
                    } else {
                        /* Non-envelope frames: pass whole payload */
                        gw->on_op(idx, (int)req_op,
                                  (const char *)frame->payload, frame->payload_len,
                                  gw->on_op_ctx);
                    }
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
            client_offline(gw, idx);
            miku_ws_frame_destroy(frame);
            return;
        }
    }
    miku_ws_frame_destroy(frame);
}

static void on_client_io(int fd, int events, void *data) {
    miku_msggw_t *gw = (miku_msggw_t *)data;
    int idx = find_client_by_fd(gw, fd);
    if (idx < 0) {
        if (gw->io) miku_io_del(gw->io, fd);
        close(fd);
        return;
    }
    if (events & (MK_IO_ERROR | MK_IO_HUP)) {
        client_offline(gw, idx);
        return;
    }
    if (events & MK_IO_READ) {
        if (!gw->clients[idx].upgraded) {
            char uid[64] = {0};
            int platform = 0;
            if (do_ws_upgrade(gw->clients[idx].fd, uid, sizeof(uid), &platform) != 0) {
                client_offline(gw, idx);
            } else {
                gw->clients[idx].upgraded = true;
                strncpy(gw->clients[idx].user_id, uid, sizeof(gw->clients[idx].user_id) - 1);
                gw->clients[idx].platform = platform;
                user_index_add(gw, idx, uid);
                if (gw->on_presence)
                    gw->on_presence(uid, platform, 1, gw->on_presence_ctx);
            }
        } else {
            read_client_frames(gw, idx);
        }
    }
}

static void on_listen_io(int fd, int events, void *data) {
    (void)events;
    miku_msggw_t *gw = (miku_msggw_t *)data;
    /* Edge-triggered: drain accept queue */
    for (;;) {
        int cfd = accept(fd, NULL, NULL);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            break;
        }
        miku_set_nonblocking(cfd);
        int idx = find_or_add_client(gw, cfd);
        if (idx < 0) {
            close(cfd);
            continue;
        }
        /* Register before reading so ET does not miss pending handshake bytes. */
        if (miku_io_add(gw->io, cfd, MK_IO_READ, on_client_io, gw) != 0) {
            client_offline(gw, idx);
            continue;
        }
        /* Opportunistic handshake if bytes already queued */
        char uid[64] = {0};
        int platform = 0;
        int urc = do_ws_upgrade(cfd, uid, sizeof(uid), &platform);
        if (urc == 0) {
            gw->clients[idx].upgraded = true;
            strncpy(gw->clients[idx].user_id, uid, sizeof(gw->clients[idx].user_id) - 1);
            gw->clients[idx].platform = platform;
            user_index_add(gw, idx, uid);
            if (gw->on_presence)
                gw->on_presence(uid, platform, 1, gw->on_presence_ctx);
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
            /* Hard failure (bad token / malformed) — drop */
            /* do_ws_upgrade already wrote 401 when applicable */
            client_offline(gw, idx);
        }
        /* EAGAIN: wait for on_client_io to complete handshake */
    }
}

int miku_msggw_poll(miku_msggw_t *gw, int timeout_ms) {
    if (!gw || !gw->running || !gw->io) return -1;

    if (!gw->listen_registered && gw->listen_fd >= 0) {
        if (miku_io_add(gw->io, gw->listen_fd, MK_IO_READ, on_listen_io, gw) != 0)
            return -1;
        gw->listen_registered = 1;
    }

    return miku_io_poll(gw->io, timeout_ms);
}
