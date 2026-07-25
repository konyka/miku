#include "miku_test.h"
#include "miku_http.h"
#include "miku_http_server.h"
#include "miku_http_client.h"
#include "miku_json.h"
#include "miku_websocket.h"
#include "miku_rpc.h"
#include "miku_rpc_client.h"
#include "miku_pb.h"
#include "miku_json_util.h"
#include "miku_sha1.h"
#include "miku_middleware.h"
#include "miku_api.h"
#include "miku_version.h"
#include "miku_auth.h"
#include "miku_token.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>

/* ── HTTP Parser Tests ────────────────────────── */

void test_http_parse_get(void) {
    const char *raw = "GET /api/v1/users?limit=10 HTTP/1.1\r\n"
                      "Host: localhost:10002\r\n"
                      "Accept: application/json\r\n"
                      "\r\n";
    miku_http_request_t *req = miku_http_request_create();
    mk_assert_not_null(req);

    int parsed = miku_http_request_parse(req, raw, strlen(raw));
    mk_assert(parsed > 0);
    mk_assert_int_eq(MK_HTTP_GET, (int)req->method);
    mk_assert_int_eq(13, (int)req->path.len);
    mk_assert(memcmp(req->path.data, "/api/v1/users", 13) == 0);
    mk_assert_int_eq(8, (int)req->query_string.len);
    mk_assert(memcmp(req->query_string.data, "limit=10", 8) == 0);

    const char *host = (const char *)miku_hashmap_get(req->headers, "host");
    mk_assert_not_null(host);
    mk_assert_str_eq("localhost:10002", host);

    const char *accept = (const char *)miku_hashmap_get(req->headers, "accept");
    mk_assert_not_null(accept);
    mk_assert_str_eq("application/json", accept);

    miku_http_request_destroy(req);
}

void test_http_parse_post_body(void) {
    const char *raw = "POST /auth/user_token HTTP/1.1\r\n"
                      "Content-Type: application/json\r\n"
                      "Content-Length: 35\r\n"
                      "\r\n"
                      "{\"userID\":\"test\",\"secret\":\"pass\"}";
    miku_http_request_t *req = miku_http_request_create();
    mk_assert_not_null(req);

    int parsed = miku_http_request_parse(req, raw, strlen(raw));
    mk_assert(parsed > 0);
    mk_assert_int_eq(MK_HTTP_POST, (int)req->method);
    mk_assert_int_eq(16, (int)req->path.len);
    mk_assert(memcmp(req->path.data, "/auth/user_token", 16) == 0);

    const char *ct = (const char *)miku_hashmap_get(req->headers, "content-type");
    mk_assert_not_null(ct);
    mk_assert_str_eq("application/json", ct);

    mk_assert_not_null(req->body.data);
    mk_assert_int_eq(33, (int)req->body.len);
    mk_assert(memcmp(req->body.data, "{\"userID\":\"test\",\"secret\":\"pass\"}", 33) == 0);

    miku_http_request_destroy(req);
}

void test_http_parse_incomplete(void) {
    const char *raw = "GET /test HTTP/1.1\r\nHost: localhost";
    miku_http_request_t *req = miku_http_request_create();
    mk_assert_not_null(req);

    int parsed = miku_http_request_parse(req, raw, strlen(raw));
    mk_assert_int_eq(0, parsed); /* incomplete - no \r\n\r\n */

    miku_http_request_destroy(req);
}

void test_http_parse_methods(void) {
    struct {
        const char *method;
        miku_http_method_t expected;
    } cases[] = {
        {"GET", MK_HTTP_GET},
        {"POST", MK_HTTP_POST},
        {"PUT", MK_HTTP_PUT},
        {"DELETE", MK_HTTP_DELETE},
        {"PATCH", MK_HTTP_PATCH},
        {"HEAD", MK_HTTP_HEAD},
        {"OPTIONS", MK_HTTP_OPTIONS},
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    for (int i = 0; i < n; i++) {
        miku_http_method_t m = miku_http_method_from_str(cases[i].method, strlen(cases[i].method));
        mk_assert_int_eq((int)cases[i].expected, (int)m);
    }
}

void test_http_response_serialize(void) {
    miku_http_response_t *resp = miku_http_response_create();
    mk_assert_not_null(resp);
    mk_assert_int_eq(200, resp->status);

    miku_http_response_set_json(resp, "{\"msg\":\"ok\"}");

    miku_string_t *out = miku_http_response_serialize(resp);
    mk_assert_not_null(out);
    mk_assert(out->len > 0);

    /* Must start with HTTP/1.1 200 */
    mk_assert(memcmp(out->data, "HTTP/1.1 200 OK\r\n", 17) == 0);
    /* Must contain Content-Type: application/json */
    mk_assert(strstr(out->data, "Content-Type: application/json") != NULL);
    /* Must contain Content-Length: 12 */
    mk_assert(strstr(out->data, "Content-Length: 12") != NULL);
    /* Must end with body */
    mk_assert(strstr(out->data, "{\"msg\":\"ok\"}") != NULL);

    miku_str_destroy(out);
    miku_http_response_destroy(resp);
}

void test_http_status_text(void) {
    mk_assert_str_eq("OK", miku_http_status_text(200));
    mk_assert_str_eq("Created", miku_http_status_text(201));
    mk_assert_str_eq("Bad Request", miku_http_status_text(400));
    mk_assert_str_eq("Not Found", miku_http_status_text(404));
    mk_assert_str_eq("Internal Server Error", miku_http_status_text(500));
}

void test_http_method_name(void) {
    mk_assert_str_eq("GET", miku_http_method_name(MK_HTTP_GET));
    mk_assert_str_eq("POST", miku_http_method_name(MK_HTTP_POST));
    mk_assert_str_eq("PUT", miku_http_method_name(MK_HTTP_PUT));
    mk_assert_str_eq("DELETE", miku_http_method_name(MK_HTTP_DELETE));
    mk_assert_str_eq("PATCH", miku_http_method_name(MK_HTTP_PATCH));
    mk_assert_str_eq("HEAD", miku_http_method_name(MK_HTTP_HEAD));
    mk_assert_str_eq("OPTIONS", miku_http_method_name(MK_HTTP_OPTIONS));
}

/* ── HTTP Server Integration Test ─────────────── */

static void ping_handler(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req;
    (void)ctx;
    resp->status = 200;
    miku_str_clear(resp->body);
    miku_str_cat(resp->body, "pong");
}

static void *server_thread(void *arg) {
    miku_http_server_t *srv = (miku_http_server_t *)arg;
    miku_http_server_start(srv);
    return NULL;
}

void test_http_server_ping(void) {
    /* Create server on a high port */
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19876);
    mk_assert_not_null(srv);

    miku_http_server_route(srv, "GET", "/ping", ping_handler, NULL);

    /* Start server in background thread */
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000); /* wait for server to start listening */

    /* Connect as client */
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    mk_assert(fd >= 0);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(19876);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    mk_assert_int_eq(0, rc);

    const char *req = "GET /ping HTTP/1.1\r\nHost: localhost\r\n\r\n";
    send(fd, req, strlen(req), 0);

    char buf[1024];
    memset(buf, 0, sizeof(buf));
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    mk_assert(n > 0);

    /* Should get HTTP/1.1 200 OK with "pong" body */
    mk_assert(memcmp(buf, "HTTP/1.1 200 OK", 15) == 0);
    mk_assert(strstr(buf, "pong") != NULL);

    close(fd);
    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
}

static void push_echo_handler(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req; (void)ctx;
    miku_http_response_set_json(resp,
        "{\"errCode\":0,\"serverMsgID\":\"gw_smid\",\"seq\":42,\"sendTime\":99}");
}

static void internal_auth_handler(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)ctx;
    const char *s = req && req->headers
        ? (const char *)miku_hashmap_get(req->headers, MIKU_INTERNAL_SECRET_HEADER) : NULL;
    if (!s) s = req && req->headers
        ? (const char *)miku_hashmap_get(req->headers, "x-internal-secret") : NULL;
    if (!s || strcmp(s, miku_internal_secret()) != 0) {
        miku_http_response_set_json(resp, "{\"errCode\":403}");
        resp->status = 403;
        return;
    }
    miku_http_response_set_json(resp, "{\"errCode\":0,\"ok\":1}");
}

void test_http_post_json_resp(void) {
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19877);
    mk_assert_not_null(srv);
    miku_http_server_route(srv, "POST", "/internal/push_msg", push_echo_handler, NULL);
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000);

    char body[256] = {0};
    int rc = miku_http_post_json_resp("http://127.0.0.1:19877/internal/push_msg",
                                      "{\"sendID\":\"a\",\"recvID\":\"b\"}",
                                      body, sizeof(body));
    mk_assert_int_eq(0, rc);
    mk_assert(strstr(body, "\"seq\":42") != NULL);
    mk_assert(strstr(body, "gw_smid") != NULL);

    miku_json_val_t *j = miku_json_parse_str(body);
    mk_assert_not_null(j);
    mk_assert_int_eq(42, (int)miku_json_int(miku_json_get(j, "seq")));
    mk_assert_str_eq("gw_smid", miku_json_str(miku_json_get(j, "serverMsgID")));
    miku_json_destroy(j);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
}

void test_http_post_json_internal_resp(void) {
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 19878);
    mk_assert_not_null(srv);
    miku_http_server_route(srv, "POST", "/internal/kick", internal_auth_handler, NULL);
    pthread_t tid;
    pthread_create(&tid, NULL, server_thread, srv);
    usleep(100000);

    char body[256] = {0};
    mk_assert_int_eq(-1, miku_http_post_json_resp("http://127.0.0.1:19878/internal/kick",
                                                  "{\"userID\":\"u1\"}", body, sizeof(body)));
    mk_assert_int_eq(0, miku_http_post_json_internal_resp("http://127.0.0.1:19878/internal/kick",
                                                          "{\"userID\":\"u1\"}",
                                                          body, sizeof(body)));
    mk_assert(strstr(body, "\"ok\":1") != NULL);

    miku_http_server_stop(srv);
    pthread_join(tid, NULL);
    miku_http_server_destroy(srv);
}

void test_rpc_build_method_payload(void) {
    miku_json_val_t *req = miku_json_create_object();
    miku_jss(req, "userID", "u\"1");
    miku_jss(req, "nickname", "n");
    char payload[256];
    mk_assert_int_eq(0, miku_rpc_build_method_payload("registerUser", req, payload, sizeof(payload)));
    miku_json_val_t *j = miku_json_parse_str(payload);
    mk_assert_not_null(j);
    mk_assert_str_eq("registerUser", miku_json_str(miku_json_get(j, "method")));
    mk_assert_str_eq("u\"1", miku_json_str(miku_json_get(j, "userID")));
    miku_json_destroy(j);
    miku_json_destroy(req);

    char empty[64];
    mk_assert_int_eq(0, miku_rpc_build_method_payload("ping", NULL, empty, sizeof(empty)));
    mk_assert_str_eq("{\"method\":\"ping\"}", empty);
}

/* ── JSON Parser Tests ───────────────────────── */

void test_json_parse_object(void) {
    const char *json = "{\"name\":\"alice\",\"age\":30,\"active\":true}";
    miku_json_val_t *v = miku_json_parse_str(json);
    mk_assert_not_null(v);
    mk_assert_int_eq((int)MK_JSON_OBJECT, (int)miku_json_type(v));
    mk_assert_int_eq(3, (int)miku_json_size(v));

    miku_json_val_t *name = miku_json_get(v, "name");
    mk_assert_not_null(name);
    mk_assert_int_eq((int)MK_JSON_STRING, (int)miku_json_type(name));
    mk_assert_str_eq("alice", miku_json_str(name));

    miku_json_val_t *age = miku_json_get(v, "age");
    mk_assert_not_null(age);
    mk_assert_int_eq((int)MK_JSON_INT, (int)miku_json_type(age));
    mk_assert_int_eq(30, (int)miku_json_int(age));

    miku_json_val_t *active = miku_json_get(v, "active");
    mk_assert_not_null(active);
    mk_assert_int_eq((int)MK_JSON_BOOL, (int)miku_json_type(active));
    mk_assert_true(miku_json_bool(active));

    miku_json_destroy(v);
}

void test_json_parse_array(void) {
    const char *json = "[1,\"hello\",true,null,3.14]";
    miku_json_val_t *v = miku_json_parse_str(json);
    mk_assert_not_null(v);
    mk_assert_int_eq((int)MK_JSON_ARRAY, (int)miku_json_type(v));
    mk_assert_int_eq(5, (int)miku_json_size(v));

    mk_assert_int_eq(1, (int)miku_json_int(miku_json_at(v, 0)));
    mk_assert_str_eq("hello", miku_json_str(miku_json_at(v, 1)));
    mk_assert_true(miku_json_bool(miku_json_at(v, 2)));
    mk_assert_int_eq((int)MK_JSON_NULL, (int)miku_json_type(miku_json_at(v, 3)));

    miku_json_val_t *dbl = miku_json_at(v, 4);
    mk_assert_int_eq((int)MK_JSON_DOUBLE, (int)miku_json_type(dbl));
    mk_assert(dbl->u.dbl_val > 3.13 && dbl->u.dbl_val < 3.15);

    miku_json_destroy(v);
}

void test_json_parse_primitives(void) {
    miku_json_val_t *t = miku_json_parse_str("true");
    mk_assert_not_null(t);
    mk_assert_true(miku_json_bool(t));
    miku_json_destroy(t);

    miku_json_val_t *f = miku_json_parse_str("false");
    mk_assert_not_null(f);
    mk_assert_false(miku_json_bool(f));
    miku_json_destroy(f);

    miku_json_val_t *n = miku_json_parse_str("null");
    mk_assert_not_null(n);
    mk_assert_int_eq((int)MK_JSON_NULL, (int)miku_json_type(n));
    miku_json_destroy(n);

    miku_json_val_t *i = miku_json_parse_str("-42");
    mk_assert_not_null(i);
    mk_assert_int_eq(-42, (int)miku_json_int(i));
    miku_json_destroy(i);
}

void test_json_parse_nested(void) {
    const char *json = "{\"users\":[{\"id\":1,\"name\":\"bob\"},{\"id\":2,\"name\":\"carol\"}],\"count\":2}";
    miku_json_val_t *v = miku_json_parse_str(json);
    mk_assert_not_null(v);

    miku_json_val_t *users = miku_json_get(v, "users");
    mk_assert_not_null(users);
    mk_assert_int_eq(2, (int)miku_json_size(users));

    miku_json_val_t *user0 = miku_json_at(users, 0);
    mk_assert_not_null(user0);
    mk_assert_str_eq("bob", miku_json_str(miku_json_get(user0, "name")));
    mk_assert_int_eq(1, (int)miku_json_int(miku_json_get(user0, "id")));

    miku_json_val_t *user1 = miku_json_at(users, 1);
    mk_assert_not_null(user1);
    mk_assert_str_eq("carol", miku_json_str(miku_json_get(user1, "name")));

    mk_assert_int_eq(2, (int)miku_json_int(miku_json_get(v, "count")));
    miku_json_destroy(v);
}

void test_json_stringify(void) {
    const char *json = "{\"key\":\"value\",\"num\":42}";
    miku_json_val_t *v = miku_json_parse_str(json);
    mk_assert_not_null(v);

    miku_string_t *out = miku_json_stringify(v);
    mk_assert_not_null(out);
    mk_assert(out->len > 0);
    mk_assert(strstr(out->data, "\"key\":\"value\"") != NULL);
    mk_assert(strstr(out->data, "\"num\":42") != NULL);

    miku_str_destroy(out);
    miku_json_destroy(v);
}

void test_json_stringify_escapes_keys(void) {
    miku_json_val_t *obj = miku_json_create_object();
    /* A conversationID reaches miku_json_object_set as a key on the WS
     * GET_CONV_MAX_READ_SEQ path, so keys are user-controlled. */
    miku_json_object_set(obj, "si_a\"b\\c", miku_json_create_int(7));
    miku_json_object_set(obj, "ctl\x01key", miku_json_create_int(8));

    miku_string_t *out = miku_json_stringify(obj);
    mk_assert_not_null(out);
    mk_assert(strstr(out->data, "\\\"") != NULL);
    mk_assert(strstr(out->data, "\\u0001") != NULL);

    miku_json_val_t *back = miku_json_parse_str(out->data);
    mk_assert_not_null(back);
    mk_assert_int_eq(7, (int)miku_json_int(miku_json_get(back, "si_a\"b\\c")));
    mk_assert_int_eq(8, (int)miku_json_int(miku_json_get(back, "ctl\x01key")));

    miku_json_destroy(back);
    miku_str_destroy(out);
    miku_json_destroy(obj);
}

void test_json_stringify_escapes_control_chars(void) {
    miku_json_val_t *obj = miku_json_create_object();
    miku_json_object_set(obj, "content", miku_json_create_str("a\x01\x1f" "b\tc"));

    miku_string_t *out = miku_json_stringify(obj);
    mk_assert_not_null(out);
    mk_assert(strstr(out->data, "\\u0001") != NULL);
    mk_assert(strstr(out->data, "\\u001f") != NULL);
    mk_assert(strstr(out->data, "\\t") != NULL);

    miku_json_val_t *back = miku_json_parse_str(out->data);
    mk_assert_not_null(back);
    mk_assert_str_eq("a\x01\x1f" "b\tc", miku_json_str(miku_json_get(back, "content")));

    miku_json_destroy(back);
    miku_str_destroy(out);
    miku_json_destroy(obj);
}

void test_json_parse_unicode_escape(void) {
    miku_json_val_t *v = miku_json_parse_str(
        "{\"a\":\"\\u0041\",\"b\":\"\\u4e2d\",\"c\":\"\\ud83d\\ude00\","
        "\"d\":\"\\ud800x\",\"e\":\"\\u0000\"}");
    mk_assert_not_null(v);

    mk_assert_str_eq("A", miku_json_str(miku_json_get(v, "a")));
    mk_assert_str_eq("\xe4\xb8\xad", miku_json_str(miku_json_get(v, "b")));
    mk_assert_str_eq("\xf0\x9f\x98\x80", miku_json_str(miku_json_get(v, "c")));
    /* Lone surrogate and NUL both become U+FFFD (EF BF BD). */
    mk_assert_str_eq("\xef\xbf\xbdx", miku_json_str(miku_json_get(v, "d")));
    mk_assert_str_eq("\xef\xbf\xbd", miku_json_str(miku_json_get(v, "e")));

    miku_json_destroy(v);
}

void test_json_build_and_query(void) {
    miku_json_val_t *obj = miku_json_create_object();
    mk_assert_not_null(obj);
    mk_assert_int_eq((int)MK_JSON_OBJECT, (int)miku_json_type(obj));

    miku_json_object_set(obj, "status", miku_json_create_int(200));
    miku_json_object_set(obj, "message", miku_json_create_str("OK"));

    miku_json_val_t *arr = miku_json_create_array();
    miku_json_array_push(arr, miku_json_create_str("item1"));
    miku_json_array_push(arr, miku_json_create_str("item2"));
    miku_json_object_set(obj, "items", arr);

    mk_assert_int_eq(3, (int)miku_json_size(obj));
    mk_assert_int_eq(200, (int)miku_json_int(miku_json_get(obj, "status")));
    mk_assert_str_eq("OK", miku_json_str(miku_json_get(obj, "message")));

    miku_json_val_t *items = miku_json_get(obj, "items");
    mk_assert_int_eq(2, (int)miku_json_size(items));
    mk_assert_str_eq("item1", miku_json_str(miku_json_at(items, 0)));
    mk_assert_str_eq("item2", miku_json_str(miku_json_at(items, 1)));

    miku_json_destroy(obj);
}

void test_json_get_missing(void) {
    miku_json_val_t *obj = miku_json_create_object();
    mk_assert_null(miku_json_get(obj, "nonexistent"));
    mk_assert_null(miku_json_at(obj, 99));
    miku_json_val_t *null_val = miku_json_create_null();
    mk_assert_int_eq(0, (int)miku_json_size(null_val));
    miku_json_destroy(null_val);

    mk_assert_int_eq((int)MK_JSON_NULL, (int)miku_json_type(NULL));
    mk_assert_int_eq(0, (int)miku_json_int(NULL));
    mk_assert_null(miku_json_str(NULL));
    miku_json_destroy(obj);
}

void test_json_roundtrip(void) {
    miku_json_val_t *obj = miku_json_create_object();
    miku_json_object_set(obj, "escaped", miku_json_create_str("line1\nline2\ttab"));
    miku_json_val_t *inner = miku_json_create_object();
    miku_json_object_set(inner, "deep", miku_json_create_bool(true));
    miku_json_object_set(obj, "nested", inner);

    miku_string_t *out = miku_json_stringify(obj);
    mk_assert_not_null(out);

    miku_json_val_t *parsed = miku_json_parse_str(out->data);
    mk_assert_not_null(parsed);
    mk_assert_str_eq("line1\nline2\ttab", miku_json_str(miku_json_get(parsed, "escaped")));

    miku_json_val_t *deep = miku_json_get(miku_json_get(parsed, "nested"), "deep");
    mk_assert_not_null(deep);
    mk_assert_true(miku_json_bool(deep));

    miku_json_destroy(parsed);
    miku_str_destroy(out);
    miku_json_destroy(obj);
}

/* ── SHA1 Tests ─────────────────────────────── */

void test_sha1_basic(void) {
    const char *input = "abc";
    uint8_t digest[20];
    miku_sha1(digest, (const uint8_t *)input, strlen(input));
    uint8_t expected[] = {
        0xA9, 0x99, 0x3E, 0x36, 0x47, 0x06, 0x81, 0x6A, 0xBA, 0x3E,
        0x25, 0x71, 0x78, 0x50, 0xC2, 0x6C, 0x9C, 0xD0, 0xD8, 0x9D
    };
    mk_assert(memcmp(digest, expected, 20) == 0);
}

void test_sha1_empty(void) {
    uint8_t digest[20];
    miku_sha1(digest, (const uint8_t *)"", 0);
    uint8_t expected[] = {
        0xDA, 0x39, 0xA3, 0xEE, 0x5E, 0x6B, 0x4B, 0x0D, 0x32, 0x55,
        0xBF, 0xEF, 0x95, 0x60, 0x18, 0x90, 0xAF, 0xD8, 0x07, 0x09
    };
    mk_assert(memcmp(digest, expected, 20) == 0);
}

/* ── WebSocket Frame Tests ──────────────────── */

void test_ws_frame_encode_decode(void) {
    miku_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.fin = true;
    f.opcode = MK_WS_TEXT;
    f.masked = false;
    const char *msg = "Hello";
    f.payload = (uint8_t *)msg;
    f.payload_len = 5;

    uint8_t buf[64];
    size_t out_len = 0;
    int rc = miku_ws_frame_encode(&f, buf, sizeof(buf), &out_len);
    mk_assert_int_eq(0, rc);
    mk_assert(out_len == 7);

    mk_assert((buf[0] & 0x80) != 0);
    mk_assert((buf[0] & 0x0F) == 0x01);
    mk_assert((buf[1] & 0x80) == 0);
    mk_assert((buf[1] & 0x7F) == 5);
    mk_assert(memcmp(buf + 2, "Hello", 5) == 0);

    miku_ws_frame_t f2;
    memset(&f2, 0, sizeof(f2));
    size_t consumed = 0;
    rc = miku_ws_frame_decode(&f2, buf, out_len, &consumed);
    mk_assert(rc > 0);
    mk_assert_int_eq((int)MK_WS_TEXT, (int)f2.opcode);
    mk_assert_true(f2.fin);
    mk_assert_false(f2.masked);
    mk_assert_int_eq(5, (int)f2.payload_len);
    mk_assert(memcmp(f2.payload, "Hello", 5) == 0);
    free(f2.payload);
}

void test_ws_frame_masked(void) {
    miku_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    f.fin = true;
    f.opcode = MK_WS_BINARY;
    f.masked = true;
    f.masking_key[0] = 0x37;
    f.masking_key[1] = 0xfa;
    f.masking_key[2] = 0x21;
    f.masking_key[3] = 0x3d;
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    f.payload = data;
    f.payload_len = 4;

    uint8_t buf[64];
    size_t out_len = 0;
    mk_assert_int_eq(0, miku_ws_frame_encode(&f, buf, sizeof(buf), &out_len));
    mk_assert((buf[1] & 0x80) != 0);

    miku_ws_frame_t f2;
    memset(&f2, 0, sizeof(f2));
    size_t consumed = 0;
    int rc = miku_ws_frame_decode(&f2, buf, out_len, &consumed);
    mk_assert(rc > 0);
    mk_assert_int_eq((int)MK_WS_BINARY, (int)f2.opcode);
    mk_assert_int_eq(4, (int)f2.payload_len);
    mk_assert(memcmp(f2.payload, data, 4) == 0);
    free(f2.payload);
}

void test_ws_frame_oversized_rejected(void) {
    /* A frame declaring more than MK_WS_MAX_PAYLOAD must be rejected outright,
     * not reported as "need more data": the caller has to close the connection
     * because the declared payload can never be skipped safely. */
    uint8_t hdr[10] = {0x81, 127, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    miku_ws_frame_t f;
    memset(&f, 0, sizeof(f));
    size_t consumed = 0;
    mk_assert_int_eq(-1, miku_ws_frame_decode(&f, hdr, sizeof(hdr), &consumed));
    mk_assert_null(f.payload);

    /* Encoding must not wrap hdr + payload_len past the output capacity. */
    uint8_t body[4] = {1, 2, 3, 4};
    miku_ws_frame_t big;
    memset(&big, 0, sizeof(big));
    big.fin = true;
    big.opcode = MK_WS_TEXT;
    big.payload = body;
    big.payload_len = UINT64_MAX - 8;
    uint8_t out[64];
    size_t out_len = 0;
    mk_assert_int_eq(-1, miku_ws_frame_encode(&big, out, sizeof(out), &out_len));

    /* A non-NULL length with a NULL payload is rejected rather than copied. */
    miku_ws_frame_t nul;
    memset(&nul, 0, sizeof(nul));
    nul.fin = true;
    nul.opcode = MK_WS_PONG;
    nul.payload = NULL;
    nul.payload_len = 16;
    mk_assert_int_eq(-1, miku_ws_frame_encode(&nul, out, sizeof(out), &out_len));
}

void test_ws_frame_read_oversized_no_desync(void) {
    int sv[2];
    mk_assert_int_eq(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));

    /* Oversized PING: previously frame_read skipped the payload but returned a
     * positive length, leaving payload NULL with a huge payload_len — the
     * caller then ponged from a NULL buffer and the unread bytes were parsed
     * as the next frame header. */
    uint8_t evil[10] = {0x89, 127, 0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF};
    mk_assert(write(sv[1], evil, sizeof(evil)) == (ssize_t)sizeof(evil));

    miku_ws_frame_t *f = miku_ws_frame_create();
    mk_assert_not_null(f);
    mk_assert_int_eq(-1, miku_ws_frame_read(sv[0], f));
    mk_assert_null(f->payload);
    mk_assert_int_eq(0, (int)f->payload_len);
    miku_ws_frame_destroy(f);

    close(sv[0]);
    close(sv[1]);
}

void test_ws_frame_read_split_header(void) {
    int sv[2];
    mk_assert_int_eq(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));

    /* Header and payload arriving in separate segments must still decode as one
     * frame; a short read used to be treated as an error, losing the consumed
     * bytes and desyncing every later parse on the connection. */
    uint8_t head[2] = {0x81, 5};
    mk_assert(write(sv[1], head, 2) == 2);
    mk_assert(write(sv[1], "He", 2) == 2);
    mk_assert(write(sv[1], "llo", 3) == 3);

    miku_ws_frame_t *f = miku_ws_frame_create();
    mk_assert_not_null(f);
    mk_assert_int_eq(5, miku_ws_frame_read(sv[0], f));
    mk_assert_int_eq((int)MK_WS_TEXT, (int)f->opcode);
    mk_assert_int_eq(5, (int)f->payload_len);
    mk_assert(memcmp(f->payload, "Hello", 5) == 0);
    miku_ws_frame_destroy(f);

    close(sv[0]);
    close(sv[1]);
}

typedef struct {
    int    fd;
    size_t payload_len;
    int    chunks;
} ws_drip_ctx_t;

static void *ws_drip_writer(void *arg) {
    ws_drip_ctx_t *d = (ws_drip_ctx_t *)arg;
    uint8_t hdr[4] = {0x82, 126,
                      (uint8_t)((d->payload_len >> 8) & 0xFF),
                      (uint8_t)(d->payload_len & 0xFF)};
    if (write(d->fd, hdr, sizeof(hdr)) != (ssize_t)sizeof(hdr)) return NULL;
    uint8_t *body = (uint8_t *)malloc(d->payload_len);
    if (!body) return NULL;
    for (size_t i = 0; i < d->payload_len; i++) body[i] = (uint8_t)(i & 0xFF);
    size_t per = d->payload_len / (size_t)d->chunks;
    size_t sent = 0;
    for (int c = 0; c < d->chunks; c++) {
        size_t n = (c == d->chunks - 1) ? d->payload_len - sent : per;
        if (write(d->fd, body + sent, n) != (ssize_t)n) break;
        sent += n;
        usleep(20000); /* force the reader to EAGAIN and poll again each time */
    }
    free(body);
    return NULL;
}

void test_ws_frame_read_multi_segment_payload(void) {
    int sv[2];
    mk_assert_int_eq(0, socketpair(AF_UNIX, SOCK_STREAM, 0, sv));

    /* A payload dripped in several segments needs more than one poll wait; a
     * single-shot wait budget would abort partway through a legitimate frame. */
    ws_drip_ctx_t d = { .fd = sv[1], .payload_len = 4096, .chunks = 5 };
    pthread_t th;
    mk_assert_int_eq(0, pthread_create(&th, NULL, ws_drip_writer, &d));

    miku_ws_frame_t *f = miku_ws_frame_create();
    mk_assert_not_null(f);
    mk_assert_int_eq(4096, miku_ws_frame_read(sv[0], f));
    mk_assert_int_eq((int)MK_WS_BINARY, (int)f->opcode);
    mk_assert_int_eq(4096, (int)f->payload_len);
    mk_assert_not_null(f->payload);
    for (int i = 0; i < 4096; i++)
        mk_assert_int_eq(i & 0xFF, (int)f->payload[i]);
    miku_ws_frame_destroy(f);

    pthread_join(th, NULL);
    close(sv[0]);
    close(sv[1]);
}

void test_ws_handshake(void) {
    const char *key = "dGhlIHNhbXBsZSBub25jZQ==";
    char accept[64];
    int rc = miku_ws_handshake(key, accept, sizeof(accept));
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=", accept);
}

/* ── Binary RPC Tests ───────────────────────── */

void test_rpc_header_codec(void) {
    miku_rpc_header_t hdr;
    miku_rpc_header_init(&hdr, MK_RPC_CALL, 42, 1001, 7);
    mk_assert_int_eq((int)MK_RPC_MAGIC, (int)hdr.magic);
    mk_assert_int_eq(1, (int)hdr.version);
    mk_assert_int_eq((int)MK_RPC_CALL, (int)hdr.msg_type);
    mk_assert_int_eq(42, (int)hdr.seq);
    mk_assert_int_eq(1001, (int)hdr.service);
    mk_assert_int_eq(7, (int)hdr.method);

    uint8_t buf[16];
    mk_assert_int_eq(16, miku_rpc_header_encode(&hdr, buf));

    miku_rpc_header_t hdr2;
    mk_assert_int_eq(16, miku_rpc_header_decode(&hdr2, buf));
    mk_assert_int_eq((int)MK_RPC_MAGIC, (int)hdr2.magic);
    mk_assert_int_eq(42, (int)hdr2.seq);
    mk_assert_int_eq(1001, (int)hdr2.service);
    mk_assert_int_eq(7, (int)hdr2.method);
}

void test_rpc_json_internal_token(void) {
    char out[512];
    mk_assert_int_eq(0, miku_rpc_json_add_internal_token(
        "{\"method\":\"ping\"}", out, sizeof(out)));
    miku_json_val_t *j = miku_json_parse_str(out);
    mk_assert_not_null(j);
    mk_assert_str_eq(miku_internal_secret(),
        miku_json_str(miku_json_get(j, "internalToken")));
    mk_assert_str_eq("ping", miku_json_str(miku_json_get(j, "method")));
    miku_json_destroy(j);
}

void test_rpc_message_roundtrip(void) {
    miku_rpc_message_t *msg = miku_rpc_message_create(MK_RPC_CALL, 1, 100, 5);
    mk_assert_not_null(msg);

    const char *payload = "{\"userID\":\"alice\"}";
    miku_rpc_message_set_payload(msg, (const uint8_t *)payload, strlen(payload));

    uint8_t *encoded = NULL;
    size_t enc_len = 0;
    mk_assert_int_eq(0, miku_rpc_message_encode(msg, &encoded, &enc_len));
    mk_assert_not_null(encoded);
    mk_assert(enc_len == MK_RPC_HDR_SIZE + 4 + strlen(payload));

    miku_rpc_message_t *decoded = miku_rpc_message_decode(encoded, enc_len);
    mk_assert_not_null(decoded);
    mk_assert_int_eq((int)MK_RPC_CALL, (int)decoded->header.msg_type);
    mk_assert_int_eq(1, (int)decoded->header.seq);
    mk_assert_int_eq(100, (int)decoded->header.service);
    mk_assert_int_eq(5, (int)decoded->header.method);
    mk_assert_int_eq((int)strlen(payload), (int)decoded->payload_len);
    mk_assert(memcmp(decoded->payload, payload, strlen(payload)) == 0);

    free(encoded);
    miku_rpc_message_destroy(decoded);
    miku_rpc_message_destroy(msg);
}

/* ── Protobuf Codec Tests ───────────────────── */

void test_pb_varint_roundtrip(void) {
    miku_pb_buf_t *buf = miku_pb_buf_create(64);
    mk_assert_not_null(buf);

    miku_pb_write_varint(buf, 1, 150);
    miku_pb_write_string(buf, 2, "testing");
    miku_pb_write_bool(buf, 3, true);

    miku_pb_reader_t r;
    miku_pb_reader_init(&r, buf->data, buf->len);

    uint32_t field;
    miku_pb_wire_t wt;

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_int_eq(1, (int)field);
    mk_assert_int_eq((int)MK_PB_VARINT, (int)wt);
    uint64_t val;
    mk_assert_true(miku_pb_read_varint(&r, &val));
    mk_assert_int_eq(150, (int)val);

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_int_eq(2, (int)field);
    const uint8_t *str_data;
    size_t str_len;
    mk_assert_true(miku_pb_read_bytes(&r, &str_data, &str_len));
    mk_assert_int_eq(7, (int)str_len);
    mk_assert(memcmp(str_data, "testing", 7) == 0);

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_int_eq(3, (int)field);
    uint64_t bval;
    mk_assert_true(miku_pb_read_varint(&r, &bval));
    mk_assert_int_eq(1, (int)bval);

    miku_pb_buf_destroy(buf);
}

void test_pb_svarint_roundtrip(void) {
    miku_pb_buf_t *buf = miku_pb_buf_create(64);
    mk_assert_not_null(buf);

    miku_pb_write_svarint(buf, 1, -1);
    miku_pb_write_svarint(buf, 2, 42);
    miku_pb_write_svarint(buf, 3, -100);

    miku_pb_reader_t r;
    miku_pb_reader_init(&r, buf->data, buf->len);

    uint32_t field;
    miku_pb_wire_t wt;
    int64_t val;

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_true(miku_pb_read_svarint(&r, &val));
    mk_assert_int_eq(-1, (int)val);

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_true(miku_pb_read_svarint(&r, &val));
    mk_assert_int_eq(42, (int)val);

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_true(miku_pb_read_svarint(&r, &val));
    mk_assert_int_eq(-100, (int)val);

    miku_pb_buf_destroy(buf);
}

void test_pb_fixed_roundtrip(void) {
    miku_pb_buf_t *buf = miku_pb_buf_create(64);
    mk_assert_not_null(buf);

    miku_pb_write_fixed32(buf, 1, 0xDEADBEEF);
    miku_pb_write_fixed64(buf, 2, 0xCAFEBABEDEADBEEFULL);

    miku_pb_reader_t r;
    miku_pb_reader_init(&r, buf->data, buf->len);

    uint32_t field;
    miku_pb_wire_t wt;

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_int_eq((int)MK_PB_FIXED32, (int)wt);
    uint32_t f32;
    mk_assert_true(miku_pb_read_fixed32(&r, &f32));
    mk_assert_int_eq((int)0xDEADBEEF, (int)f32);

    mk_assert_true(miku_pb_read_field(&r, &field, &wt));
    mk_assert_int_eq((int)MK_PB_FIXED64, (int)wt);
    uint64_t f64;
    mk_assert_true(miku_pb_read_fixed64(&r, &f64));
    mk_assert(f64 == 0xCAFEBABEDEADBEEFULL);

    miku_pb_buf_destroy(buf);
}

void test_mw_request_id_rejects_crlf(void);
void test_http_response_header_crlf_stripped(void);
void test_mw_auth_skips_auth_paths(void);
void test_mw_auth_skips_health_and_version(void);
void test_mw_auth_rejects_no_token(void);
void test_mw_auth_rejects_bad_token(void);
void test_mw_auth_accepts_valid_token(void);
void test_api_routes_all_registered(void);
void test_api_route_handler_responds(void);
void test_api_auth_login(void);
void test_api_version(void);
void test_api_health(void);
void test_api_all_203_routes(void);

void run_protocol_tests(void) {
    printf("── Miku Protocol Tests ───────────────────\n\n");

    mk_run_test(test_http_parse_get);
    mk_run_test(test_http_parse_post_body);
    mk_run_test(test_http_parse_incomplete);
    mk_run_test(test_http_parse_methods);
    mk_run_test(test_http_response_serialize);
    mk_run_test(test_http_status_text);
    mk_run_test(test_http_method_name);
    mk_run_test(test_http_server_ping);
    mk_run_test(test_http_post_json_resp);
    mk_run_test(test_http_post_json_internal_resp);

    printf("\n");
    mk_run_test(test_json_parse_object);
    mk_run_test(test_json_parse_array);
    mk_run_test(test_json_parse_primitives);
    mk_run_test(test_json_parse_nested);
    mk_run_test(test_json_stringify);
    mk_run_test(test_json_stringify_escapes_keys);
    mk_run_test(test_json_stringify_escapes_control_chars);
    mk_run_test(test_json_parse_unicode_escape);
    mk_run_test(test_json_build_and_query);
    mk_run_test(test_json_get_missing);
    mk_run_test(test_json_roundtrip);

    printf("\n");
    mk_run_test(test_sha1_basic);
    mk_run_test(test_sha1_empty);
    mk_run_test(test_ws_frame_encode_decode);
    mk_run_test(test_ws_frame_masked);
    mk_run_test(test_ws_frame_oversized_rejected);
    mk_run_test(test_ws_frame_read_oversized_no_desync);
    mk_run_test(test_ws_frame_read_split_header);
    mk_run_test(test_ws_frame_read_multi_segment_payload);
    mk_run_test(test_ws_handshake);
    mk_run_test(test_rpc_header_codec);
    mk_run_test(test_rpc_json_internal_token);
    mk_run_test(test_rpc_build_method_payload);
    mk_run_test(test_rpc_message_roundtrip);
    mk_run_test(test_pb_varint_roundtrip);
    mk_run_test(test_pb_svarint_roundtrip);
    mk_run_test(test_pb_fixed_roundtrip);

    printf("\n");
    mk_run_test(test_mw_request_id_rejects_crlf);
    mk_run_test(test_http_response_header_crlf_stripped);
    mk_run_test(test_mw_auth_skips_auth_paths);
    mk_run_test(test_mw_auth_skips_health_and_version);
    mk_run_test(test_mw_auth_rejects_no_token);
    mk_run_test(test_mw_auth_rejects_bad_token);
    mk_run_test(test_mw_auth_accepts_valid_token);
    mk_run_test(test_api_routes_all_registered);
    mk_run_test(test_api_route_handler_responds);
    mk_run_test(test_api_auth_login);
    mk_run_test(test_api_version);
    mk_run_test(test_api_health);
    mk_run_test(test_api_all_203_routes);
}

static miku_http_request_t *make_req(const char *method, const char *path, const char *body) {
    static char buf[4096];
    int len;
    if (body) {
        len = snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\nContent-Type: application/json\r\nContent-Length: %zu\r\n\r\n%s", method, path, strlen(body), body);
    } else {
        len = snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\n\r\n", method, path);
    }
    miku_http_request_t *req = miku_http_request_create();
    miku_http_request_parse(req, buf, (size_t)len);
    return req;
}

static miku_http_request_t *make_req_with_token(const char *method, const char *path, const char *body, const char *token) {
    static char buf[4096];
    int len;
    if (body) {
        len = snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\nContent-Type: application/json\r\ntoken: %s\r\nContent-Length: %zu\r\n\r\n%s", method, path, token, strlen(body), body);
    } else {
        len = snprintf(buf, sizeof(buf), "%s %s HTTP/1.1\r\ntoken: %s\r\n\r\n", method, path, token);
    }
    miku_http_request_t *req = miku_http_request_create();
    miku_http_request_parse(req, buf, (size_t)len);
    return req;
}

void test_mw_request_id_rejects_crlf(void) {
    /* operationID is client-supplied and miku_mw_request_id echoes it into the
     * X-Request-ID response header, before any auth middleware runs. The request
     * parser splits header lines on CRLF, so a full CRLF cannot survive it — but
     * a bare LF does, and RFC 7230 §3.5 notes recipients may accept a lone LF as
     * a line terminator, so it must not reach the serialised header block. */
    static char buf[1024];
    int len = snprintf(buf, sizeof(buf),
        "GET /version HTTP/1.1\r\n"
        "operationID: abc\nX-Injected: evil\r\n"
        "\r\n");
    miku_http_request_t *req = miku_http_request_create();
    miku_http_request_parse(req, buf, (size_t)len);

    /* Confirm the parser really did hand the middleware an LF-bearing value. */
    const char *raw_hdr = (const char *)miku_hashmap_get(req->headers, "operationid");
    mk_assert_not_null(raw_hdr);
    mk_assert_not_null(strchr(raw_hdr, '\n'));

    miku_http_response_t *resp = miku_http_response_create();
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_request_id(req, resp, NULL));

    miku_string_t *raw = miku_http_response_serialize(resp);
    mk_assert_not_null(raw);
    mk_assert_null(strstr(raw->data, "X-Injected"));
    /* The header block must still terminate exactly once, at its real end. */
    const char *sep = strstr(raw->data, "\r\n\r\n");
    mk_assert_not_null(sep);
    mk_assert_null(strstr(sep + 4, "\r\n\r\n"));

    miku_str_destroy(raw);
    miku_http_response_destroy(resp);
    miku_http_request_destroy(req);
}

void test_http_response_header_crlf_stripped(void) {
    /* Backstop at the serialiser: even if a caller puts a raw CRLF value into
     * resp->headers, the wire format must not gain an extra header or an early
     * end-of-headers. */
    miku_http_response_t *resp = miku_http_response_create();
    miku_hashmap_put(resp->headers, "X-Test",
                     strdup("ok\r\nX-Smuggled: 1\r\n\r\nbody"));
    miku_string_t *raw = miku_http_response_serialize(resp);
    mk_assert_not_null(raw);
    mk_assert_null(strstr(raw->data, "X-Smuggled"));
    const char *sep = strstr(raw->data, "\r\n\r\n");
    mk_assert_not_null(sep);
    mk_assert_null(strstr(sep + 4, "\r\n\r\n"));
    miku_str_destroy(raw);
    miku_http_response_destroy(resp);
}

void test_mw_auth_skips_auth_paths(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    miku_http_request_t *req = make_req("POST", "/auth/user_token", "{}");
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
    miku_http_request_destroy(req);

    req = make_req("POST", "/auth/parse_token", "{}");
    mk_assert_int_eq((int)MK_MW_STOP, (int)miku_mw_auth(req, resp, &cfg));
    mk_assert_int_eq(401, resp->status);
    miku_http_request_destroy(req);

    /* force_logout requires a valid token */
    req = make_req("POST", "/auth/force_logout", "{}");
    mk_assert_int_eq((int)MK_MW_STOP, (int)miku_mw_auth(req, resp, &cfg));
    mk_assert_int_eq(401, resp->status);
    miku_http_request_destroy(req);

    miku_http_response_destroy(resp);
}

void test_mw_auth_skips_health_and_version(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    miku_http_request_t *req = make_req("GET", "/admin/health", NULL);
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
    miku_http_request_destroy(req);

    req = make_req("GET", "/version", NULL);
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
    miku_http_request_destroy(req);

    miku_http_response_destroy(resp);
}

void test_mw_auth_rejects_no_token(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    miku_http_request_t *req = make_req("POST", "/user/register", "{}");
    mk_assert_int_eq((int)MK_MW_STOP, (int)miku_mw_auth(req, resp, &cfg));
    mk_assert_int_eq(401, resp->status);
    miku_http_request_destroy(req);
    miku_http_response_destroy(resp);
}

void test_mw_auth_rejects_bad_token(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    miku_http_request_t *req = make_req_with_token("POST", "/user/register", "{}", "bad_token");
    mk_assert_int_eq((int)MK_MW_STOP, (int)miku_mw_auth(req, resp, &cfg));
    mk_assert_int_eq(401, resp->status);
    miku_http_request_destroy(req);
    miku_http_response_destroy(resp);
}

void test_mw_auth_accepts_valid_token(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    char token[512] = {0};
    mk_assert_int_eq(0, miku_token_create("testuser", 1, "openIM123", token, sizeof(token)));
    miku_http_request_t *req = make_req_with_token("POST", "/user/register", "{}", token);
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
    miku_http_request_destroy(req);
    miku_http_response_destroy(resp);
}

void test_api_routes_all_registered(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 0);
    mk_assert_not_null(srv);
    int rc = miku_api_register_routes(srv, ctx);
    mk_assert_int_eq(0, rc);
    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}

void test_api_route_handler_responds(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);

    static const char *paths[] = {
        "/friend/add", "/friend/delete", "/friend/get_friend_list",
        "/friend/is_friend", "/friend/add_black", "/friend/remove_black",
        "/friend/get_black_list", "/group/create", "/group/join",
        "/group/quit", "/group/dismiss", "/group/mute",
        "/conversation/get_all", "/conversation/get_conv", "/conversation/set",
        "/msg/send", "/msg/get", "/msg/revoke",
        "/third/upload_token", "/third/download_url",
        NULL
    };

    for (int i = 0; paths[i]; i++) {
        miku_http_request_t *req = make_req_with_token("POST", paths[i], "{}", "miku_user_abc_1");
        miku_http_response_t *resp = miku_http_response_create();

        miku_json_val_t *rpc_req = miku_json_parse_str("{}");
        miku_json_val_t *rpc_resp = miku_json_create_object();
        miku_friend_handle_rpc(ctx->friend_svc, "getFriendList", rpc_req, rpc_resp);
        miku_json_destroy(rpc_resp);
        miku_json_destroy(rpc_req);

        mk_assert_not_null(resp);
        miku_http_request_destroy(req);
        miku_http_response_destroy(resp);
    }

    miku_api_ctx_destroy(ctx);
}

void test_api_auth_login(void) {
    miku_auth_service_t *svc = miku_auth_service_create();
    mk_assert_not_null(svc);

    char token[512] = {0};
    int rc = miku_auth_user_token(svc, "testuser", "openIM123", 1, token, sizeof(token));
    mk_assert_int_eq(0, rc);
    mk_assert(strncmp(token, "miku|", 5) == 0);

    char uid[64] = {0};
    rc = miku_auth_parse_token(svc, token, uid, sizeof(uid));
    mk_assert_int_eq(0, rc);
    mk_assert_str_eq("testuser", uid);

    miku_auth_service_destroy(svc);
}

void test_api_version(void) {
    mk_assert_not_null(MIKU_VERSION_STRING);
    mk_assert(strlen(MIKU_VERSION_STRING) > 0);
    mk_assert_not_null(MIKU_GIT_HASH);
}

void test_api_health(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    miku_stats_init(&ctx->stats, "test", 0);

    miku_stats_snapshot_t snap;
    miku_stats_snapshot(&ctx->stats, &snap);
     mk_assert_int_eq(0, (int)snap.requests_total);

     miku_api_ctx_destroy(ctx);
}

void test_api_all_203_routes(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 0);
    mk_assert_not_null(srv);
    int rc = miku_api_register_routes(srv, ctx);
    mk_assert_int_eq(0, rc);
    mk_assert_int_eq(203, miku_http_server_route_count(srv));

    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}
