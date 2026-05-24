#include "miku_http.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

miku_http_request_t *miku_http_request_create(void) {
    miku_http_request_t *req = (miku_http_request_t *)calloc(1, sizeof(*req));
    if (!req) return NULL;
    req->headers = miku_hashmap_create(16, free);
    req->version = 11;
    return req;
}

static bool parse_request_line(miku_http_request_t *req, const char *line, size_t len) {
    const char *sp1 = (const char *)memchr(line, ' ', len);
    if (!sp1) return false;
    size_t after_method = (size_t)(sp1 + 1 - line);
    if (after_method >= len) return false;
    const char *sp2 = (const char *)memchr(sp1 + 1, ' ', len - after_method);
    if (!sp2) return false;

    req->method = miku_http_method_from_str(line, (size_t)(sp1 - line));
    req->path = (miku_str_t){sp1 + 1, (size_t)(sp2 - sp1 - 1)};

    const char *qm = (const char *)memchr(req->path.data, '?', req->path.len);
    if (qm) {
        req->query_string = (miku_str_t){qm + 1, (size_t)(req->path.data + req->path.len - qm - 1)};
        req->path.len = (size_t)(qm - req->path.data);
    }
    return true;
}

static bool parse_header(miku_http_request_t *req, const char *line, size_t len) {
    const char *colon = (const char *)memchr(line, ':', len);
    if (!colon) return false;
    size_t klen = (size_t)(colon - line);
    const char *val = colon + 1;
    size_t vlen = len - klen - 1;
    while (vlen > 0 && val[0] == ' ') { val++; vlen--; }
    char *key = strndup(line, klen);
    char *value = strndup(val, vlen);
    for (char *p = key; *p; p++) if (*p >= 'A' && *p <= 'Z') *p += 32;
    miku_hashmap_put(req->headers, key, value);
    free(key);
    return true;
}

int miku_http_request_parse(miku_http_request_t *req, const char *data, size_t len) {
    if (!req || !data) return -1;
    const char *hdr_end = (const char *)memmem(data, len, "\r\n\r\n", 4);
    if (!hdr_end) return 0;
    size_t hdr_len = (size_t)(hdr_end - data);
    const char *p = data;
    const char *end = data + hdr_len;
    const char *line_start = p;
    int line_count = 0;
    while (p <= end) {
        if (p == end || (p + 1 < data + len && p[0] == '\r' && p[1] == '\n')) {
            size_t ll = (size_t)(p - line_start);
            if (ll > 0) {
                if (line_count == 0) {
                    if (!parse_request_line(req, line_start, ll)) return -1;
                } else {
                    parse_header(req, line_start, ll);
                }
            }
            line_count++;
            line_start = p + 2;
            p += 2;
        } else {
            p++;
        }
    }
    size_t body_offset = hdr_len + 4;
    if (body_offset < len) {
        req->body = (miku_str_t){data + body_offset, len - body_offset};
    }
    return (int)(hdr_len + 4);
}

void miku_http_request_destroy(miku_http_request_t *req) {
    if (!req) return;
    miku_hashmap_destroy(req->headers);
    free(req);
}

miku_http_response_t *miku_http_response_create(void) {
    miku_http_response_t *resp = (miku_http_response_t *)calloc(1, sizeof(*resp));
    if (!resp) return NULL;
    resp->status = 200;
    resp->body = miku_str_create("");
    resp->headers = miku_hashmap_create(8, free);
    return resp;
}

void miku_http_response_set_json(miku_http_response_t *resp, const char *json) {
    if (!resp || !json) return;
    miku_str_clear(resp->body);
    miku_str_cat(resp->body, json);
    miku_hashmap_put(resp->headers, strdup("Content-Type"), strdup("application/json"));
}

static void add_header_cb(const char *key, void *val, void *ctx) {
    miku_string_t *s = (miku_string_t *)ctx;
    miku_str_printf(s, "%s: %s\r\n", key, (const char *)val);
}

miku_string_t *miku_http_response_serialize(const miku_http_response_t *resp) {
    if (!resp) return NULL;
    miku_string_t *s = miku_str_create_empty(256);
    miku_str_printf(s, "HTTP/1.1 %d %s\r\n", resp->status, miku_http_status_text(resp->status));
    miku_hashmap_foreach(resp->headers, add_header_cb, s);

    char cl[32];
    snprintf(cl, sizeof(cl), "%zu", resp->body ? resp->body->len : (size_t)0);
    miku_str_printf(s, "Content-Length: %s\r\n\r\n", cl);

    if (resp->body && resp->body->len > 0)
        miku_str_cat_len(s, resp->body->data, resp->body->len);

    return s;
}

void miku_http_response_destroy(miku_http_response_t *resp) {
    if (!resp) return;
    if (resp->body) miku_str_destroy(resp->body);
    miku_hashmap_destroy(resp->headers);
    free(resp);
}

const char *miku_http_status_text(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

const char *miku_http_method_name(miku_http_method_t method) {
    switch (method) {
        case MK_HTTP_GET:     return "GET";
        case MK_HTTP_POST:    return "POST";
        case MK_HTTP_PUT:     return "PUT";
        case MK_HTTP_DELETE:  return "DELETE";
        case MK_HTTP_PATCH:   return "PATCH";
        case MK_HTTP_HEAD:    return "HEAD";
        case MK_HTTP_OPTIONS: return "OPTIONS";
        default:              return "UNKNOWN";
    }
}

miku_http_method_t miku_http_method_from_str(const char *s, size_t len) {
    if (len == 3 && strncasecmp(s, "GET", 3) == 0) return MK_HTTP_GET;
    if (len == 4 && strncasecmp(s, "POST", 4) == 0) return MK_HTTP_POST;
    if (len == 3 && strncasecmp(s, "PUT", 3) == 0) return MK_HTTP_PUT;
    if (len == 6 && strncasecmp(s, "DELETE", 6) == 0) return MK_HTTP_DELETE;
    if (len == 5 && strncasecmp(s, "PATCH", 5) == 0) return MK_HTTP_PATCH;
    if (len == 4 && strncasecmp(s, "HEAD", 4) == 0) return MK_HTTP_HEAD;
    if (len == 7 && strncasecmp(s, "OPTIONS", 7) == 0) return MK_HTTP_OPTIONS;
    return MK_HTTP_GET;
}
