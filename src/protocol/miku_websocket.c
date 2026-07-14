#include "miku_websocket.h"
#include "miku_log.h"
#include "miku_sha1.h"
#include "miku_base64.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdio.h>

#define MK_WS_FD_MAP 65536

static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

int miku_ws_handshake(const char *sec_ws_key, char *accept_out, size_t accept_cap) {
    if (!sec_ws_key || !accept_out) return -1;
    size_t klen = strlen(sec_ws_key);
    size_t mlen = strlen(WS_MAGIC);
    size_t combined_len = klen + mlen;
    uint8_t *combined = (uint8_t *)malloc(combined_len);
    if (!combined) return -1;
    memcpy(combined, sec_ws_key, klen);
    memcpy(combined + klen, WS_MAGIC, mlen);

    uint8_t hash[20];
    miku_sha1(hash, combined, combined_len);
    free(combined);

    size_t b64_len = miku_base64_encode(hash, 20, accept_out, accept_cap - 1);
    accept_out[b64_len] = '\0';
    return 0;
}

miku_ws_frame_t *miku_ws_frame_create(void) {
    miku_ws_frame_t *f = (miku_ws_frame_t *)calloc(1, sizeof(*f));
    return f;
}

void miku_ws_frame_destroy(miku_ws_frame_t *f) {
    if (f) free(f->payload);
    free(f);
}

int miku_ws_frame_encode(const miku_ws_frame_t *f, uint8_t *out, size_t out_cap, size_t *out_len) {
    if (!f || !out || !out_len) return -1;
    size_t need = 2;
    uint64_t plen = f->payload_len;
    if (plen <= 125) {
        need += 0;
    } else if (plen <= 65535) {
        need += 2;
    } else {
        need += 8;
    }
    if (f->masked) need += 4;
    need += plen;
    if (need > out_cap) return -1;

    size_t pos = 0;
    out[pos++] = (uint8_t)((f->fin ? 0x80 : 0x00) | (f->opcode & 0x0F));
    uint8_t mask_bit = f->masked ? 0x80 : 0x00;

    if (plen <= 125) {
        out[pos++] = mask_bit | (uint8_t)plen;
    } else if (plen <= 65535) {
        out[pos++] = mask_bit | 126;
        out[pos++] = (uint8_t)((plen >> 8) & 0xFF);
        out[pos++] = (uint8_t)(plen & 0xFF);
    } else {
        out[pos++] = mask_bit | 127;
        for (int i = 7; i >= 0; i--)
            out[pos++] = (uint8_t)((plen >> (i * 8)) & 0xFF);
    }

    if (f->masked) {
        memcpy(out + pos, f->masking_key, 4);
        pos += 4;
        for (uint64_t i = 0; i < plen; i++)
            out[pos + i] = f->payload[i] ^ f->masking_key[i & 3];
        pos += plen;
    } else {
        if (plen > 0 && f->payload)
            memcpy(out + pos, f->payload, (size_t)plen);
        pos += (size_t)plen;
    }

    *out_len = pos;
    return 0;
}

int miku_ws_frame_decode(miku_ws_frame_t *f, const uint8_t *data, size_t len, size_t *consumed) {
    if (!f || !data || len < 2) return -1;
    size_t pos = 0;

    uint8_t b0 = data[pos++];
    uint8_t b1 = data[pos++];
    f->fin = (b0 & 0x80) != 0;
    f->opcode = (miku_ws_opcode_t)(b0 & 0x0F);
    f->masked = (b1 & 0x80) != 0;
    uint64_t plen = b1 & 0x7F;

    if (plen == 126) {
        if (pos + 2 > len) return 0;
        plen = ((uint64_t)data[pos] << 8) | data[pos + 1];
        pos += 2;
    } else if (plen == 127) {
        if (pos + 8 > len) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | data[pos++];
    }

    if (f->masked) {
        if (pos + 4 > len) return 0;
        memcpy(f->masking_key, data + pos, 4);
        pos += 4;
    }

    if (pos + plen > len) return 0;

    free(f->payload);
    f->payload = NULL;
    f->payload_len = plen;
    if (plen > 0) {
        f->payload = (uint8_t *)malloc((size_t)plen);
        if (!f->payload) return -1;
        if (f->masked) {
            for (uint64_t i = 0; i < plen; i++)
                f->payload[i] = data[pos + i] ^ f->masking_key[i & 3];
        } else {
            memcpy(f->payload, data + pos, (size_t)plen);
        }
    }
    pos += (size_t)plen;

    if (consumed) *consumed = pos;
    return (int)pos;
}

int miku_ws_frame_read(int fd, miku_ws_frame_t *f) {
    uint8_t hdr[2];
    ssize_t n = read(fd, hdr, 2);
    if (n < 2) return -1;

    f->fin = (hdr[0] & 0x80) != 0;
    f->opcode = (miku_ws_opcode_t)(hdr[0] & 0x0F);
    f->masked = (hdr[1] & 0x80) != 0;
    uint64_t plen = hdr[1] & 0x7F;

    if (plen == 126) {
        uint8_t ext[2];
        if (read(fd, ext, 2) < 2) return -1;
        plen = ((uint64_t)ext[0] << 8) | ext[1];
    } else if (plen == 127) {
        uint8_t ext[8];
        if (read(fd, ext, 8) < 8) return -1;
        plen = 0;
        for (int i = 0; i < 8; i++)
            plen = (plen << 8) | ext[i];
    }

    if (f->masked) {
        if (read(fd, f->masking_key, 4) < 4) return -1;
    }

    free(f->payload);
    f->payload = NULL;
    f->payload_len = plen;
    if (plen > 0 && plen < 1048576) {
        f->payload = (uint8_t *)malloc((size_t)plen);
        if (!f->payload) return -1;
        size_t total_read = 0;
        while (total_read < plen) {
            ssize_t r = read(fd, f->payload + total_read, (size_t)plen - total_read);
            if (r <= 0) { free(f->payload); f->payload = NULL; return -1; }
            total_read += (size_t)r;
        }
        if (f->masked) {
            for (uint64_t i = 0; i < plen; i++)
                f->payload[i] ^= f->masking_key[i & 3];
        }
    }
    return (int)plen;
}

static int ws_send_frame(int fd, miku_ws_opcode_t opcode, const uint8_t *data, size_t len) {
    miku_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.fin = true;
    f.opcode = opcode;
    f.masked = false;
    f.payload = (uint8_t *)data;
    f.payload_len = len;

    uint8_t buf[4096];
    if (len + 14 > sizeof(buf)) return -1;
    size_t out_len = 0;
    if (miku_ws_frame_encode(&f, buf, sizeof(buf), &out_len) != 0) return -1;
    ssize_t sent = write(fd, buf, out_len);
    return (sent == (ssize_t)out_len) ? 0 : -1;
}

int miku_ws_send_text(int fd, const char *text, size_t len) {
    return ws_send_frame(fd, MK_WS_TEXT, (const uint8_t *)text, len);
}

int miku_ws_send_binary(int fd, const uint8_t *data, size_t len) {
    return ws_send_frame(fd, MK_WS_BINARY, data, len);
}

int miku_ws_send_close(int fd, uint16_t code, const char *reason) {
    uint8_t buf[128];
    buf[0] = (uint8_t)(code >> 8);
    buf[1] = (uint8_t)(code & 0xFF);
    size_t rlen = reason ? strlen(reason) : 0;
    if (rlen > sizeof(buf) - 2) rlen = sizeof(buf) - 2;
    if (rlen > 0) memcpy(buf + 2, reason, rlen);
    return ws_send_frame(fd, MK_WS_CLOSE, buf, 2 + rlen);
}

int miku_ws_send_pong(int fd, const uint8_t *data, size_t len) {
    return ws_send_frame(fd, MK_WS_PONG, data, len);
}

#define MAX_WS_CLIENTS 1024

struct miku_ws_server_s {
    int                  listen_fd;
    char                 host[64];
    int                  port;
    miku_io_t           *io;
    bool                 running;
    miku_ws_conn_t       clients[MAX_WS_CLIENTS];
    int                  client_count;
    int16_t              fd_map[MK_WS_FD_MAP]; /* fd → client idx, -1 empty */
    miku_ws_on_connect_fn on_connect;
    void                *on_connect_ctx;
    miku_ws_on_message_fn on_message;
    void                *on_message_ctx;
    miku_ws_on_close_fn   on_close;
    void                *on_close_ctx;
};

static miku_ws_conn_t *ws_find_conn(miku_ws_server_t *srv, int fd) {
    if (fd >= 0 && fd < MK_WS_FD_MAP) {
        int idx = srv->fd_map[fd];
        if (idx >= 0 && idx < srv->client_count && srv->clients[idx].fd == fd)
            return &srv->clients[idx];
    }
    for (int i = 0; i < srv->client_count; i++) {
        if (srv->clients[i].fd == fd) return &srv->clients[i];
    }
    return NULL;
}

static void ws_handle_client(int fd, int events, void *data) {
    (void)events;
    miku_ws_server_t *srv = (miku_ws_server_t *)data;
    miku_ws_conn_t *conn = ws_find_conn(srv, fd);
    if (!conn) return;

    miku_ws_frame_t *f = miku_ws_frame_create();
    int rc = miku_ws_frame_read(fd, f);
    if (rc < 0) {
        if (srv->on_close) srv->on_close(conn, srv->on_close_ctx);
        if (fd >= 0 && fd < MK_WS_FD_MAP) srv->fd_map[fd] = -1;
        close(fd);
        miku_io_del(srv->io, fd);
        conn->fd = -1;
        miku_ws_frame_destroy(f);
        return;
    }

    if (f->opcode == MK_WS_PING) {
        miku_ws_send_pong(fd, f->payload, (size_t)f->payload_len);
    } else if (f->opcode == MK_WS_CLOSE) {
        miku_ws_send_close(fd, 1000, "bye");
        if (srv->on_close) srv->on_close(conn, srv->on_close_ctx);
        if (fd >= 0 && fd < MK_WS_FD_MAP) srv->fd_map[fd] = -1;
        close(fd);
        miku_io_del(srv->io, fd);
        conn->fd = -1;
    } else if (srv->on_message) {
        srv->on_message(conn, f->opcode, f->payload, (size_t)f->payload_len, srv->on_message_ctx);
    }
    miku_ws_frame_destroy(f);
}

static void ws_accept_conn(int fd, int events, void *data) {
    (void)events;
    miku_ws_server_t *srv = (miku_ws_server_t *)data;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr *)&addr, &addrlen);
    if (client_fd < 0) return;

    char buf[4096];
    ssize_t n = read(client_fd, buf, sizeof(buf) - 1);
    if (n <= 0) { close(client_fd); return; }
    buf[n] = '\0';

    char *key_start = strstr(buf, "Sec-WebSocket-Key: ");
    if (!key_start) { close(client_fd); return; }
    key_start += 19;
    char *key_end = strchr(key_start, '\r');
    if (!key_end) { close(client_fd); return; }
    char ws_key[128];
    size_t key_len = (size_t)(key_end - key_start);
    if (key_len >= sizeof(ws_key)) { close(client_fd); return; }
    memcpy(ws_key, key_start, key_len);
    ws_key[key_len] = '\0';

    char accept_val[64];
    if (miku_ws_handshake(ws_key, accept_val, sizeof(accept_val)) != 0) {
        close(client_fd);
        return;
    }

    char resp[512];
    int resp_len = snprintf(resp, sizeof(resp),
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: %s\r\n\r\n",
        accept_val);
    write(client_fd, resp, (size_t)resp_len);

    miku_set_nonblocking(client_fd);

    if (srv->client_count < MAX_WS_CLIENTS) {
        int idx = srv->client_count++;
        miku_ws_conn_t *conn = &srv->clients[idx];
        conn->fd = client_fd;
        conn->server = srv;
        inet_ntop(AF_INET, &addr.sin_addr, conn->addr, sizeof(conn->addr));
        if (client_fd >= 0 && client_fd < MK_WS_FD_MAP)
            srv->fd_map[client_fd] = (int16_t)idx;
        miku_io_add(srv->io, client_fd, MK_IO_READ, ws_handle_client, srv);
        if (srv->on_connect) srv->on_connect(conn, srv->on_connect_ctx);
    } else {
        close(client_fd);
    }
}

miku_ws_server_t *miku_ws_server_create(const char *host, int port) {
    miku_ws_server_t *srv = (miku_ws_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    strncpy(srv->host, host ? host : "0.0.0.0", sizeof(srv->host) - 1);
    srv->port = port;
    srv->io = miku_io_create();
    for (int i = 0; i < MAX_WS_CLIENTS; i++) srv->clients[i].fd = -1;
    for (int i = 0; i < MK_WS_FD_MAP; i++) srv->fd_map[i] = -1;
    return srv;
}

void miku_ws_server_on_connect(miku_ws_server_t *srv, miku_ws_on_connect_fn fn, void *ctx) {
    if (srv) { srv->on_connect = fn; srv->on_connect_ctx = ctx; }
}

void miku_ws_server_on_message(miku_ws_server_t *srv, miku_ws_on_message_fn fn, void *ctx) {
    if (srv) { srv->on_message = fn; srv->on_message_ctx = ctx; }
}

void miku_ws_server_on_close(miku_ws_server_t *srv, miku_ws_on_close_fn fn, void *ctx) {
    if (srv) { srv->on_close = fn; srv->on_close_ctx = ctx; }
}

int miku_ws_server_start(miku_ws_server_t *srv) {
    if (!srv) return -1;
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0) return -1;

    int opt = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    miku_set_nonblocking(srv->listen_fd);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)srv->port);
    inet_pton(AF_INET, srv->host, &addr.sin_addr);

    if (bind(srv->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(srv->listen_fd);
        return -1;
    }
    if (listen(srv->listen_fd, 128) != 0) {
        close(srv->listen_fd);
        return -1;
    }

    miku_io_add(srv->io, srv->listen_fd, MK_IO_READ, ws_accept_conn, srv);
    srv->running = true;

    while (srv->running) {
        miku_io_poll(srv->io, 100);
    }
    return 0;
}

void miku_ws_server_stop(miku_ws_server_t *srv) {
    if (srv) srv->running = false;
}

void miku_ws_server_destroy(miku_ws_server_t *srv) {
    if (!srv) return;
    for (int i = 0; i < srv->client_count; i++) {
        if (srv->clients[i].fd >= 0) close(srv->clients[i].fd);
    }
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    if (srv->io) miku_io_destroy(srv->io);
    free(srv);
}
