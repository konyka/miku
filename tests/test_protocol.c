#include "miku_test.h"
#include "miku_http.h"
#include "miku_http_server.h"
#include "miku_json.h"
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
    mk_assert_int_eq(0, (int)miku_json_size(miku_json_create_null()));

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

    printf("\n");
    mk_run_test(test_json_parse_object);
    mk_run_test(test_json_parse_array);
    mk_run_test(test_json_parse_primitives);
    mk_run_test(test_json_parse_nested);
    mk_run_test(test_json_stringify);
    mk_run_test(test_json_build_and_query);
    mk_run_test(test_json_get_missing);
    mk_run_test(test_json_roundtrip);
}
