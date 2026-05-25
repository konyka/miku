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
};

#define MIKU_DEFAULT_MAX_BODY (1 << 20)

static void handle_client(int fd, int events, void *data) {
    (void)events;
    miku_http_server_t *srv = (miku_http_server_t *)data;
    char buf[READ_BUF];
    ssize_t nread = read(fd, buf, sizeof(buf) - 1);
    if (nread <= 0) {
        if (srv->stats) miku_stats_conn_close(srv->stats);
        close(fd); miku_io_del(srv->io, fd); return;
    }
    size_t n = (size_t)nread;
    buf[n] = '\0';
    if (srv->stats) miku_stats_bytes_recv(srv->stats, (int64_t)n);

    miku_http_request_t *req = miku_http_request_create();
    int parsed = miku_http_request_parse(req, buf, n);
    if (parsed <= 0) {
        miku_http_request_destroy(req);
        const char *bad = "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\n\r\n";
        write(fd, bad, strlen(bad));
        close(fd);
        miku_io_del(srv->io, fd);
        return;
    }

    if (srv->max_body > 0 && req->body.len > srv->max_body) {
        miku_http_request_destroy(req);
        const char *big = "HTTP/1.1 413 Payload Too Large\r\nContent-Length: 0\r\n\r\n";
        write(fd, big, strlen(big));
        close(fd);
        miku_io_del(srv->io, fd);
        if (srv->stats) miku_stats_conn_close(srv->stats);
        return;
    }

    miku_http_response_t *resp = miku_http_response_create();

    for (int i = 0; i < srv->mw_count; i++) {
        miku_mw_result_t r = srv->middleware[i].fn(req, resp, srv->middleware[i].ctx);
        if (r == MK_MW_STOP) goto send_response;
    }

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

send_response:
    miku_string_t *out = miku_http_response_serialize(resp);
    write(fd, out->data, out->len);
    if (srv->stats) {
        miku_stats_bytes_sent(srv->stats, (int64_t)out->len);
        miku_stats_conn_close(srv->stats);
    }
    miku_str_destroy(out);
    miku_http_response_destroy(resp);
    miku_http_request_destroy(req);
    close(fd);
    miku_io_del(srv->io, fd);
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
    miku_io_add(srv->io, client_fd, MK_IO_READ, handle_client, srv);
}

miku_http_server_t *miku_http_server_create(const char *host, int port) {
    miku_http_server_t *srv = (miku_http_server_t *)calloc(1, sizeof(*srv));
    if (!srv) return NULL;
    strncpy(srv->host, host ? host : "0.0.0.0", sizeof(srv->host) - 1);
    srv->port = port;
    srv->io = miku_io_create();
    srv->max_body = MIKU_DEFAULT_MAX_BODY;
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
    }
    return 0;
}

void miku_http_server_stop(miku_http_server_t *srv) {
    if (srv) srv->running = false;
}

void miku_http_server_destroy(miku_http_server_t *srv) {
    if (!srv) return;
    if (srv->listen_fd >= 0) close(srv->listen_fd);
    if (srv->io) miku_io_destroy(srv->io);
    free(srv);
}

void miku_http_server_set_stats(miku_http_server_t *srv, miku_stats_t *stats) {
    if (srv) srv->stats = stats;
}

void miku_http_server_set_max_body(miku_http_server_t *srv, size_t max_bytes) {
    if (srv) srv->max_body = max_bytes;
}
