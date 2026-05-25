#include "miku_http_server.h"
#include "miku_log.h"
#include "miku_string.h"
#include "miku_stats.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>

#ifdef MIKU_ENABLE_TLS
#include <openssl/ssl.h>
#include <openssl/err.h>
#endif

#define MAX_ROUTES 256
#define MAX_MIDDLEWARE 16
#define READ_BUF   4096

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
    miku_mw_entry_t    middleware[MAX_MIDDLEWARE];
    int                mw_count;
    bool               running;
    miku_stats_t      *stats;
    size_t             max_body;
    bool               keep_alive;
    int                idle_timeout_sec;
    int               *conn_fds;
    int64_t           *conn_last_active;
    int                conn_count;
    int                conn_cap;
    bool               tls_enabled;
#ifdef MIKU_ENABLE_TLS
    SSL_CTX           *ssl_ctx;
#endif
};

#define MIKU_DEFAULT_MAX_BODY (1 << 20)
#define MIKU_INITIAL_CONN_CAP 64

static void conn_track_add(miku_http_server_t *srv, int fd) {
    if (srv->conn_count >= srv->conn_cap) {
        int newcap = srv->conn_cap * 2;
        srv->conn_fds = realloc(srv->conn_fds, (size_t)newcap * sizeof(int));
        srv->conn_last_active = realloc(srv->conn_last_active, (size_t)newcap * sizeof(int64_t));
        srv->conn_cap = newcap;
    }
    srv->conn_fds[srv->conn_count] = fd;
    srv->conn_last_active[srv->conn_count] = miku_timestamp_ms();
    srv->conn_count++;
}

static void conn_track_remove(miku_http_server_t *srv, int fd) {
    for (int i = 0; i < srv->conn_count; i++) {
        if (srv->conn_fds[i] == fd) {
            srv->conn_fds[i] = srv->conn_fds[srv->conn_count - 1];
            srv->conn_last_active[i] = srv->conn_last_active[srv->conn_count - 1];
            srv->conn_count--;
            return;
        }
    }
}

static void conn_track_touch(miku_http_server_t *srv, int fd) {
    for (int i = 0; i < srv->conn_count; i++) {
        if (srv->conn_fds[i] == fd) {
            srv->conn_last_active[i] = miku_timestamp_ms();
            return;
        }
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
            srv->conn_fds[i] = srv->conn_fds[srv->conn_count - 1];
            srv->conn_last_active[i] = srv->conn_last_active[srv->conn_count - 1];
            srv->conn_count--;
        }
    }
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

    char buf[READ_BUF];
    ssize_t nread = MIKU_READ(buf, sizeof(buf) - 1);
    if (nread <= 0) {
        conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
        if (ssl) SSL_free(ssl);
#endif
        if (srv->stats) miku_stats_conn_close(srv->stats);
        close(fd); miku_io_del(srv->io, fd); return;
    }
    conn_track_touch(srv, fd);
    size_t n = (size_t)nread;
    buf[n] = '\0';
    if (srv->stats) miku_stats_bytes_recv(srv->stats, (int64_t)n);

    miku_http_request_t *req = miku_http_request_create();
    int parsed = miku_http_request_parse(req, buf, n);
    if (parsed <= 0) {
        miku_http_request_destroy(req);
        const char *bad = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        MIKU_WRITE(bad, strlen(bad));
        conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
        if (ssl) SSL_free(ssl);
#endif
        close(fd);
        miku_io_del(srv->io, fd);
        return;
    }

    if (srv->max_body > 0 && req->body.len > srv->max_body) {
        miku_http_request_destroy(req);
        const char *big = "HTTP/1.1 413 Payload Too Large\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
        MIKU_WRITE(big, strlen(big));
        conn_track_remove(srv, fd);
#ifdef MIKU_ENABLE_TLS
        if (ssl) SSL_free(ssl);
#endif
        close(fd);
        miku_io_del(srv->io, fd);
        if (srv->stats) miku_stats_conn_close(srv->stats);
        return;
    }

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
        for (int i = 0; i < srv->route_count; i++) {
            miku_http_route_t *r = &srv->routes[i];
            const char *method = miku_http_method_name(req->method);
            if (strcasecmp(method, r->method) == 0 &&
                req->path.len == strlen(r->path) &&
                strncmp(req->path.data, r->path, req->path.len) == 0) {
                r->handler(req, resp, r->ctx);
                matched = true;
                break;
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
    if (!resp->headers) resp->headers = miku_hashmap_create(4, NULL);
    miku_hashmap_put(resp->headers, "Connection", strdup(wants_close ? "close" : "keep-alive"));
    miku_string_t *out = miku_http_response_serialize(resp);
    MIKU_WRITE(out->data, out->len);
    if (srv->stats) miku_stats_bytes_sent(srv->stats, (int64_t)out->len);
    miku_str_destroy(out);
    miku_http_response_destroy(resp);
    miku_http_request_destroy(req);

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
    srv->max_body = MIKU_DEFAULT_MAX_BODY;
    srv->keep_alive = true;
    srv->idle_timeout_sec = 30;
    srv->conn_cap = MIKU_INITIAL_CONN_CAP;
    srv->conn_fds = malloc(MIKU_INITIAL_CONN_CAP * sizeof(int));
    srv->conn_last_active = malloc(MIKU_INITIAL_CONN_CAP * sizeof(int64_t));
    srv->conn_count = 0;
    return srv;
}

int miku_http_server_route(miku_http_server_t *srv, const char *method,
                            const char *path, miku_http_handler_fn fn, void *ctx) {
    if (!srv || srv->route_count >= MAX_ROUTES) return -1;
    miku_http_route_t *r = &srv->routes[srv->route_count++];
    r->method = method;
    r->path = path;
    r->handler = fn;
    r->ctx = ctx;
    return 0;
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
