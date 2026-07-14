#include "miku_http_server.h"
#include "miku_log.h"
#include "miku_string.h"
#include "miku_stats.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_hashmap.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <stdint.h>

#ifdef MIKU_ENABLE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#define MAX_ROUTES 256
#define MAX_MIDDLEWARE 16
#define READ_BUF   65536
#define READ_CHUNK 8192
#define MIKU_HTTP_FD_MAP 65536

typedef struct {
    miku_http_middleware_fn fn;
    void                   *ctx;
} miku_mw_entry_t;

struct miku_http_server_s {
    int                listen_fd;
    char               host[64];
    int                port;
    miku_io_t         *io;
    miku_http_route_t  routes[MAX_ROUTES];
    int                route_count;
    miku_hashmap_t    *route_map;   /* "METHOD path" -> route index */
    miku_mw_entry_t    middleware[MAX_MIDDLEWARE];
    int                mw_count;
    bool               running;
    miku_stats_t      *stats;
    size_t             max_body;
    bool               keep_alive;
    int                idle_timeout_sec;
    int               *conn_fds;
    int64_t           *conn_last_active;
    int16_t            conn_fd_map[MIKU_HTTP_FD_MAP]; /* fd → conn slot, -1 empty */
    int                conn_count;
    int                conn_cap;
    bool               tls_enabled;
#ifdef MIKU_ENABLE_TLS
    SSL_CTX           *ssl_ctx;
#endif
};

#define MIKU_DEFAULT_MAX_BODY (1 << 20)
#define MIKU_INITIAL_CONN_CAP 64

static void conn_fd_map_set(miku_http_server_t *srv, int fd, int idx) {
    if (fd >= 0 && fd < MIKU_HTTP_FD_MAP) srv->conn_fd_map[fd] = (int16_t)idx;
}

static void conn_fd_map_clear(miku_http_server_t *srv, int fd) {
    if (fd >= 0 && fd < MIKU_HTTP_FD_MAP) srv->conn_fd_map[fd] = -1;
}

static int conn_fd_map_get(miku_http_server_t *srv, int fd) {
    if (fd < 0 || fd >= MIKU_HTTP_FD_MAP) return -1;
    int idx = srv->conn_fd_map[fd];
    if (idx < 0 || idx >= srv->conn_count || srv->conn_fds[idx] != fd) return -1;
    return idx;
}

static void conn_track_add(miku_http_server_t *srv, int fd) {
    int existing = conn_fd_map_get(srv, fd);
    if (existing >= 0) {
        srv->conn_last_active[existing] = miku_timestamp_ms();
        return;
    }
    if (srv->conn_count >= srv->conn_cap) {
        int newcap = srv->conn_cap * 2;
        srv->conn_fds = realloc(srv->conn_fds, (size_t)newcap * sizeof(int));
        srv->conn_last_active = realloc(srv->conn_last_active, (size_t)newcap * sizeof(int64_t));
        srv->conn_cap = newcap;
    }
    int idx = srv->conn_count++;
    srv->conn_fds[idx] = fd;
    srv->conn_last_active[idx] = miku_timestamp_ms();
    conn_fd_map_set(srv, fd, idx);
}

static void conn_track_remove(miku_http_server_t *srv, int fd) {
    int i = conn_fd_map_get(srv, fd);
    if (i < 0) {
        /* Fallback for fds outside map range */
        for (i = 0; i < srv->conn_count; i++) {
            if (srv->conn_fds[i] == fd) break;
        }
        if (i >= srv->conn_count) return;
    }
    conn_fd_map_clear(srv, fd);
    int last = srv->conn_count - 1;
    if (i != last) {
        int moved = srv->conn_fds[last];
        srv->conn_fds[i] = moved;
        srv->conn_last_active[i] = srv->conn_last_active[last];
        conn_fd_map_set(srv, moved, i);
    }
    srv->conn_count--;
}

static void conn_track_touch(miku_http_server_t *srv, int fd) {
    int i = conn_fd_map_get(srv, fd);
    if (i >= 0) {
        srv->conn_last_active[i] = miku_timestamp_ms();
        return;
    }
    conn_track_add(srv, fd);
}

static void conn_sweep_idle(miku_http_server_t *srv) {
    if (srv->idle_timeout_sec <= 0) return;
    int64_t now = miku_timestamp_ms();
    int64_t timeout_ms = (int64_t)srv->idle_timeout_sec * 1000;
    for (int i = srv->conn_count - 1; i >= 0; i--) {
        if ((now - srv->conn_last_active[i]) > timeout_ms) {
            int fd = srv->conn_fds[i];
            close(fd);
            miku_io_del(srv->io, fd);
            if (srv->stats) miku_stats_conn_close(srv->stats);
            conn_track_remove(srv, fd);
        }
    }
}

static size_t http_content_length(miku_http_request_t *req) {
    if (!req || !req->headers) return 0;
    const char *cl = (const char *)miku_hashmap_get(req->headers, "content-length");
    if (!cl || !cl[0]) return 0;
    return (size_t)strtoul(cl, NULL, 10);
}

static void handle_client(int fd, int events, void *data) {
    (void)events;
    miku_http_server_t *srv = (miku_http_server_t *)data;

#ifdef MIKU_ENABLE_TLS
    SSL *ssl = NULL;
    if (srv->ssl_ctx) {
        ssl = SSL_new(srv->ssl_ctx);
        if (!ssl) { close(fd); miku_io_del(srv->io, fd); return; }
        SSL_set_fd(ssl, fd);
        if (SSL_accept(ssl) <= 0) {
            SSL_free(ssl);
            close(fd); miku_io_del(srv->io, fd); return;
        }
    }
#define MIKU_READ(buf, sz) (ssl ? SSL_read(ssl, buf, sz) : read(fd, buf, sz))
#define MIKU_WRITE(buf, sz) (ssl ? SSL_write(ssl, buf, sz) : write(fd, buf, sz))
#else
#define MIKU_READ(buf, sz) read(fd, buf, sz)
#define MIKU_WRITE(buf, sz) write(fd, buf, sz)
#endif

    /* Growable heap buffer so body pointers remain valid for the request lifetime. */
    size_t cap = READ_BUF;
    size_t n = 0;
    char *buf = (char *)malloc(cap);
    if (!buf) {
#ifdef MIKU_ENABLE_TLS
        if (ssl) SSL_free(ssl);
#endif
        close(fd); miku_io_del(srv->io, fd); return;
    }

    int hdr_parsed = 0;
    size_t need_total = 0;
    miku_http_request_t *req = NULL;

    for (;;) {
        if (n + READ_CHUNK + 1 > cap) {
            size_t ncap = cap * 2;
            if (ncap < n + READ_CHUNK + 1) ncap = n + READ_CHUNK + 1;
            if (srv->max_body > 0 && ncap > srv->max_body + 8192) {
                free(buf);
                if (req) miku_http_request_destroy(req);
                const char *big = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                MIKU_WRITE(big, strlen(big));
                conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
                if (ssl) SSL_free(ssl);
#endif
                if (srv->stats) miku_stats_conn_close(srv->stats);
                close(fd); miku_io_del(srv->io, fd);
                return;
            }
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) {
                free(buf);
                if (req) miku_http_request_destroy(req);
#ifdef MIKU_ENABLE_TLS
                if (ssl) SSL_free(ssl);
#endif
                close(fd); miku_io_del(srv->io, fd);
                return;
            }
            buf = nb;
            cap = ncap;
        }

        ssize_t nread = MIKU_READ(buf + n, READ_CHUNK);
        if (nread <= 0) {
            free(buf);
            if (req) miku_http_request_destroy(req);
            conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
            if (ssl) SSL_free(ssl);
#endif
            if (srv->stats) miku_stats_conn_close(srv->stats);
            close(fd); miku_io_del(srv->io, fd);
            return;
        }
        n += (size_t)nread;
        buf[n] = '\0';
        conn_track_touch(srv, fd);
        if (srv->stats) miku_stats_bytes_recv(srv->stats, (int64_t)nread);

        if (!hdr_parsed) {
            if (!memmem(buf, n, "\r\n\r\n", 4)) {
                if (n > 65536) { /* headers too large */
                    free(buf);
                    const char *bad = "HTTP/1.1 431 Request Header Fields Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                    MIKU_WRITE(bad, strlen(bad));
                    conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
                    if (ssl) SSL_free(ssl);
#endif
                    if (srv->stats) miku_stats_conn_close(srv->stats);
                    close(fd); miku_io_del(srv->io, fd);
                    return;
                }
                continue;
            }
            req = miku_http_request_create();
            int parsed = miku_http_request_parse(req, buf, n);
            if (parsed <= 0) {
                free(buf);
                miku_http_request_destroy(req);
                const char *bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                MIKU_WRITE(bad, strlen(bad));
                conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
                if (ssl) SSL_free(ssl);
#endif
                close(fd); miku_io_del(srv->io, fd);
                return;
            }
            size_t cl = http_content_length(req);
            if (srv->max_body > 0 && cl > srv->max_body) {
                free(buf);
                miku_http_request_destroy(req);
                const char *big = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
                MIKU_WRITE(big, strlen(big));
                conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
                if (ssl) SSL_free(ssl);
#endif
                if (srv->stats) miku_stats_conn_close(srv->stats);
                close(fd); miku_io_del(srv->io, fd);
                return;
            }
            need_total = (size_t)parsed + cl;
            hdr_parsed = 1;
            /* Re-parse after full body arrives so body slice is exact. */
            miku_http_request_destroy(req);
            req = NULL;
        }

        if (hdr_parsed && n >= need_total) break;
    }

    req = miku_http_request_create();
    int parsed = miku_http_request_parse(req, buf, need_total > 0 ? need_total : n);
    if (parsed <= 0) {
        free(buf);
        miku_http_request_destroy(req);
        const char *bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        MIKU_WRITE(bad, strlen(bad));
        conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
        if (ssl) SSL_free(ssl);
#endif
        close(fd); miku_io_del(srv->io, fd);
        return;
    }

    /* Clamp body to Content-Length when present. */
    size_t cl = http_content_length(req);
    if (cl > 0 && req->body.len > cl) req->body.len = cl;

    bool wants_close = true;
    if (req->headers) {
        const char *conn = (const char *)miku_hashmap_get(req->headers, "connection");
        if (conn && strcasecmp(conn, "keep-alive") == 0) wants_close = false;
    } else if (srv->keep_alive && req->version >= 1) {
        wants_close = false;
    }

    miku_http_response_t *resp = miku_http_response_create();

    for (int i = 0; i < srv->mw_count; i++) {
        miku_mw_result_t r = srv->middleware[i].fn(req, resp, srv->middleware[i].ctx);
        if (r == MK_MW_STOP) goto send_response_ka;
    }

    {
        bool matched = false;
        const char *method = miku_http_method_name(req->method);
        char rkey[512];
        snprintf(rkey, sizeof(rkey), "%s %.*s", method, (int)req->path.len, req->path.data);
        void *idxp = srv->route_map ? miku_hashmap_get(srv->route_map, rkey) : NULL;
        if (idxp) {
            int idx = (int)(intptr_t)idxp - 1; /* stored as index+1 to avoid NULL */
            if (idx >= 0 && idx < srv->route_count) {
                miku_http_route_t *r = &srv->routes[idx];
                r->handler(req, resp, r->ctx);
                matched = true;
            }
        }
        if (!matched) {
            /* Fallback linear scan (should not hit when route_map is populated) */
            for (int i = 0; i < srv->route_count; i++) {
                miku_http_route_t *r = &srv->routes[i];
                if (strcasecmp(method, r->method) == 0 &&
                    req->path.len == strlen(r->path) &&
                    strncmp(req->path.data, r->path, req->path.len) == 0) {
                    r->handler(req, resp, r->ctx);
                    matched = true;
                    break;
                }
            }
        }
        if (!matched) {
            resp->status = 404;
            miku_json_val_t *err = miku_json_create_object();
            miku_jerr(err, 404, "route not found");
            miku_string_t *es = miku_json_stringify(err);
            miku_http_response_set_json(resp, es->data);
            miku_str_destroy(es);
            miku_json_destroy(err);
        }
    }

send_response_ka:
    if (!resp->headers) resp->headers = miku_hashmap_create(4, free);
    miku_hashmap_put(resp->headers, "Connection", strdup(wants_close ? "close" : "keep-alive"));
    miku_string_t *out = miku_http_response_serialize(resp);
    MIKU_WRITE(out->data, out->len);
    if (srv->stats) miku_stats_bytes_sent(srv->stats, (int64_t)out->len);
    miku_str_destroy(out);
    miku_http_response_destroy(resp);
    miku_http_request_destroy(req);
    free(buf);

#ifdef MIKU_ENABLE_TLS
    if (ssl) SSL_free(ssl);
#endif

    if (wants_close) {
        conn_track_remove(srv, fd);
        if (srv->stats) miku_stats_conn_close(srv->stats);
        close(fd);
        miku_io_del(srv->io, fd);
    }
}

static void accept_conn(int fd, int events, void *data) {
    (void)events;
    miku_http_server_t *srv = (miku_http_server_t *)data;
    struct sockaddr_in addr;
    socklen_t addrlen = sizeof(addr);
    int client_fd = accept(fd, (struct sockaddr *)&addr, &addrlen);
    if (client_fd < 0) return;
    miku_set_nonblocking(client_fd);
    if (srv->stats) miku_stats_conn_open(srv->stats);
    conn_track_add(srv, client_fd);
    miku_io_add(srv->io, client_fd, MK_IO_READ, handle_client, srv);
}

miku_http_server_t *miku_http_server_create(const char *host, int port) {
    miku_http_server_t *srv = (miku_http_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    strncpy(srv->host, host ? host : "0.0.0.0", sizeof(srv->host) - 1);
    srv->port = port;
    srv->io = miku_io_create();
    srv->route_map = miku_hashmap_create(256, NULL);
    srv->max_body = MIKU_DEFAULT_MAX_BODY;
    srv->keep_alive = true;
    srv->idle_timeout_sec = 30;
    srv->conn_cap = MIKU_INITIAL_CONN_CAP;
    srv->conn_fds = malloc(MIKU_INITIAL_CONN_CAP * sizeof(int));
    srv->conn_last_active = malloc(MIKU_INITIAL_CONN_CAP * sizeof(int64_t));
    srv->conn_count = 0;
    for (int i = 0; i < MIKU_HTTP_FD_MAP; i++) srv->conn_fd_map[i] = -1;
    return srv;
}

int miku_http_server_route(miku_http_server_t *srv, const char *method,
                            const char *path, miku_http_handler_fn fn, void *ctx) {
    if (!srv || srv->route_count >= MAX_ROUTES) return -1;
    int idx = srv->route_count;
    miku_http_route_t *r = &srv->routes[srv->route_count++];
    r->method = method;
    r->path = path;
    r->handler = fn;
    r->ctx = ctx;
    if (srv->route_map && method && path) {
        char key[512];
        snprintf(key, sizeof(key), "%s %s", method, path);
        /* Store index+1 so a zero index is distinguishable from missing */
        miku_hashmap_put(srv->route_map, key, (void *)(intptr_t)(idx + 1));
    }
    return 0;
}

int miku_http_server_route_count(const miku_http_server_t *srv) {
    return srv ? srv->route_count : 0;
}

int miku_http_server_use(miku_http_server_t *srv, miku_http_middleware_fn mw, void *ctx) {
    if (!srv || !mw || srv->mw_count >= MAX_MIDDLEWARE) return -1;
    srv->middleware[srv->mw_count].fn = mw;
    srv->middleware[srv->mw_count].ctx = ctx;
    srv->mw_count++;
    return 0;
}

int miku_http_server_start(miku_http_server_t *srv) {
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

    miku_io_add(srv->io, srv->listen_fd, MK_IO_READ, accept_conn, srv);
    srv->running = true;

    while (srv->running) {
        miku_io_poll(srv->io, 100);
        conn_sweep_idle(srv);
    }
    return 0;
}

void miku_http_server_stop(miku_http_server_t *srv) {
    if (srv) srv->running = false;
}

void miku_http_server_destroy(miku_http_server_t *srv) {
    if (!srv) return;
#ifdef MIKU_ENABLE_TLS
    if (srv->ssl_ctx) SSL_CTX_free(srv->ssl_ctx);
#endif
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    if (srv->io) miku_io_destroy(srv->io);
    if (srv->route_map) miku_hashmap_destroy(srv->route_map);
    free(srv->conn_fds);
    free(srv->conn_last_active);
    free(srv);
}

void miku_http_server_set_stats(miku_http_server_t *srv, miku_stats_t *stats) {
    if (srv) srv->stats = stats;
}

void miku_http_server_set_max_body(miku_http_server_t *srv, size_t max_bytes) {
    if (srv) srv->max_body = max_bytes;
}

void miku_http_server_set_idle_timeout(miku_http_server_t *srv, int seconds) {
    if (srv) srv->idle_timeout_sec = seconds;
}

void miku_http_server_set_tls(miku_http_server_t *srv, const char *cert_path, const char *key_path) {
    if (!srv || !cert_path || !key_path) return;
#ifdef MIKU_ENABLE_TLS
    SSL_library_init();
    SSL_load_error_strings();
    OpenSSL_add_all_algorithms();
    srv->ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!srv->ssl_ctx) {
        MK_LOG_ERROR("Failed to create SSL context");
        return;
    }
    if (SSL_CTX_use_certificate_file(srv->ssl_ctx, cert_path, SSL_FILETYPE_PEM) <= 0) {
        MK_LOG_ERROR("Failed to load certificate: %s", cert_path);
        SSL_CTX_free(srv->ssl_ctx);
        srv->ssl_ctx = NULL;
        return;
    }
    if (SSL_CTX_use_PrivateKey_file(srv->ssl_ctx, key_path, SSL_FILETYPE_PEM) <= 0) {
        MK_LOG_ERROR("Failed to load private key: %s", key_path);
        SSL_CTX_free(srv->ssl_ctx);
        srv->ssl_ctx = NULL;
        return;
    }
    srv->tls_enabled = true;
    MK_LOG_INFO("TLS enabled: cert=%s key=%s", cert_path, key_path);
#else
    (void)srv; (void)cert_path; (void)key_path;
    MK_LOG_WARN("TLS not available (build with -DMIKU_ENABLE_TLS=ON)");
#endif
}
