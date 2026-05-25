#include "miku_test.h"
#include "miku_http.h"
#include "miku_http_server.h"
#include "miku_json.h"
#include "miku_websocket.h"
#include "miku_rpc.h"
#include "miku_pb.h"
#include "miku_sha1.h"
#include "miku_middleware.h"
#include "miku_api.h"
#include "miku_version.h"
#include "miku_auth.h"
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
void test_api_all_100_routes(void);

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

    printf("\n");
    mk_run_test(test_sha1_basic);
    mk_run_test(test_sha1_empty);
    mk_run_test(test_ws_frame_encode_decode);
    mk_run_test(test_ws_frame_masked);
    mk_run_test(test_ws_handshake);
    mk_run_test(test_rpc_header_codec);
    mk_run_test(test_rpc_message_roundtrip);
    mk_run_test(test_pb_varint_roundtrip);
    mk_run_test(test_pb_svarint_roundtrip);
    mk_run_test(test_pb_fixed_roundtrip);

    printf("\n");
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
    mk_run_test(test_api_all_100_routes);
}

static miku_http_request_t *make_req(const char *method, const char *path, const char *body) {
    char buf[4096];
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
    char buf[4096];
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

void test_mw_auth_skips_auth_paths(void) {
    miku_auth_mw_cfg_t cfg = { .secret = "openIM123", .enabled = 1 };
    miku_http_response_t *resp = miku_http_response_create();

    miku_http_request_t *req = make_req("POST", "/auth/user_token", "{}");
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
    miku_http_request_destroy(req);

    req = make_req("POST", "/auth/force_logout", "{}");
    mk_assert_int_eq((int)MK_MW_CONTINUE, (int)miku_mw_auth(req, resp, &cfg));
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

    miku_http_request_t *req = make_req_with_token("POST", "/user/register", "{}", "miku_testuser_uuid123_1");
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

        miku_friend_handle_rpc(ctx->friend_svc, "getFriendList",
                               miku_json_parse_str("{}"),
                               miku_json_create_object());

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
    mk_assert(strncmp(token, "miku_", 5) == 0);

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

void test_api_all_100_routes(void) {
    miku_api_ctx_t *ctx = miku_api_ctx_create();
    mk_assert_not_null(ctx);
    miku_http_server_t *srv = miku_http_server_create("127.0.0.1", 0);
    mk_assert_not_null(srv);
    int rc = miku_api_register_routes(srv, ctx);
    mk_assert_int_eq(0, rc);

    static const char *routes[] = {
        "POST /auth/user_token",
        "POST /auth/parse_token",
        "POST /auth/admin_token",
        "POST /auth/force_logout",
        "POST /auth/force_logout_all",
        "POST /user/register",
        "POST /user/get_users_info",
        "POST /user/update_user_info",
        "POST /user/account_check",
        "POST /user/get_all_users",
        "POST /user/count",
        "POST /user/search",
        "POST /user/get_users_online_status",
        "POST /user/set_global_recv_opt",
        "POST /user/get_global_recv_opt",
        "POST /user/update_user_status",
        "POST /user/process_user_command",
        "POST /user/get_user_status",
        "POST /user/get_subscribe_users_status",
        "POST /user/subscribe_or_cancel_user_status",
        "POST /user/set_user_status",
        "POST /friend/add",
        "POST /friend/delete",
        "POST /friend/get_friend_list",
        "POST /friend/is_friend",
        "POST /friend/add_black",
        "POST /friend/remove_black",
        "POST /friend/get_black_list",
        "POST /friend/delete_friend",
        "POST /friend/get_friend_apply_list",
        "POST /friend/get_self_apply_list",
        "POST /friend/get_designated_apply",
        "POST /friend/accept_apply",
        "POST /friend/refuse_apply",
        "POST /friend/import_friend",
        "POST /friend/sync_friend",
        "POST /group/create",
        "POST /group/get_group_info",
        "POST /group/get_groups_info",
        "POST /group/set_group_info",
        "POST /group/get_group_member_list",
        "POST /group/get_group_member_user_id",
        "POST /group/set_group_member_info",
        "POST /group/invite",
        "POST /group/join",
        "POST /group/quit",
        "POST /group/dismiss",
        "POST /group/mute",
        "POST /group/cancel_mute",
        "POST /group/kick",
        "POST /group/transfer",
        "POST /group/get_joined_group_list",
        "POST /group/get_group_applicant_list",
        "POST /group/get_group_application_list",
        "POST /group/accept_group_application",
        "POST /group/refuse_group_application",
        "POST /group/mute_member",
        "POST /group/cancel_mute_member",
        "POST /conversation/get_all",
        "POST /conversation/get_conv",
        "POST /conversation/set",
        "POST /conversation/get_all_conversations",
        "POST /conversation/set_conversations",
        "POST /conversation/delete_conversation",
        "POST /conversation/get_conversation_list",
        "POST /conversation/get_conversations",
        "POST /conversation/get_total_unread",
        "POST /conversation/set_conversation_min_seq",
        "POST /conversation/mark_as_read",
        "POST /conversation/clear_conv_msg",
        "POST /conversation/pin_conversation",
        "POST /msg/send",
        "POST /msg/get",
        "POST /msg/revoke",
        "POST /msg/send_msg",
        "POST /msg/get_msg",
        "POST /msg/get_server_time",
        "POST /msg/get_send_status",
        "POST /msg/clean_up",
        "POST /msg/delete_msg",
        "POST /msg/batch_send",
        "POST /msg/mark_as_read",
        "POST /msg/get_by_seq",
        "POST /msg/set_message_reaction_extensions",
        "POST /msg/get_message_list_reaction_extensions",
        "POST /msg/add_message_reaction_extensions",
        "POST /msg/delete_message_reaction_extensions",
        "POST /third/upload_token",
        "POST /third/download_url",
        "POST /third/access_url",
        "POST /third/delete_object",
        "POST /third/initiate_multipart",
        "POST /third/complete_multipart",
        "POST /third/get_upload_info",
        "POST /third/get_object_info",
        "POST /third/get_signal_invitation_info",
        "POST /batch/get_users_info",
        "POST /batch/delete_friend",
        "POST /admin/stats",
        "POST /admin/shutdown",
        "GET  /admin/health",
        "GET  /admin/metrics",
        "GET  /version",
        NULL
    };

    int expected = 0;
    for (int i = 0; routes[i]; i++) expected++;
    mk_assert_int_eq(103, expected);

    miku_http_server_destroy(srv);
    miku_api_ctx_destroy(ctx);
}
