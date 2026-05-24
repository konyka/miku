#include "miku_api.h"
#include "miku_log.h"
#include "miku_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

miku_api_ctx_t *miku_api_ctx_create(void) {
    miku_api_ctx_t *ctx = (miku_api_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->auth = miku_auth_service_create();
    ctx->user = miku_user_service_create();
    ctx->friend_svc = miku_friend_service_create();
    ctx->group_svc = miku_group_service_create();
    ctx->conv = miku_conv_service_create();
    ctx->msg = miku_msg_service_create();
    ctx->third = miku_third_service_create();
    return ctx;
}

void miku_api_ctx_destroy(miku_api_ctx_t *ctx) {
    if (!ctx) return;
    miku_auth_service_destroy(ctx->auth);
    miku_user_service_destroy(ctx->user);
    miku_friend_service_destroy(ctx->friend_svc);
    miku_group_service_destroy(ctx->group_svc);
    miku_conv_service_destroy(ctx->conv);
    miku_msg_service_destroy(ctx->msg);
    miku_third_service_destroy(ctx->third);
    free(ctx);
}

static void json_resp(miku_http_response_t *resp, miku_json_val_t *j) {
    miku_string_t *s = miku_json_stringify(j);
    miku_http_response_set_json(resp, s->data);
    miku_str_destroy(s);
    miku_json_destroy(j);
}

static miku_json_val_t *parse_body(miku_http_request_t *req) {
    if (req->body.data && req->body.len > 0) {
        char *tmp = strndup(req->body.data, req->body.len);
        miku_json_val_t *j = miku_json_parse_str(tmp);
        free(tmp);
        return j;
    }
    return miku_json_create_object();
}

static void ji(miku_json_val_t *o, const char *k, int64_t v) { miku_json_object_set(o, k, miku_json_create_int(v)); }
static void jss(miku_json_val_t *o, const char *k, const char *v) { if (v) miku_json_object_set(o, k, miku_json_create_str(v)); }

static void handle_auth(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    if (strstr(path, "user_token")) {
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        const char *secret = miku_json_str(miku_json_get(j, "secret"));
        int64_t plat = miku_json_int(miku_json_get(j, "platformID"));
        char token[512] = {0};
        int rc = miku_auth_user_token(c->auth, uid, secret, (int)plat, token, sizeof(token));
        ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) { jss(out, "token", token); ji(out, "expireTimeSeconds", 86400); }
    } else if (strstr(path, "force_logout")) {
        ji(out, "errCode", 0);
    } else {
        ji(out, "errCode", 404);
    }
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_user(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getUserInfo";
    if (strstr(path, "register")) method = "registerUser";
    else if (strstr(path, "update")) method = "updateUserInfo";
    else if (strstr(path, "get_users")) method = "getUsersInfo";
    miku_user_handle_rpc(c->user, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_friend(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getFriendList";
    if (strstr(path, "add")) method = "addFriend";
    else if (strstr(path, "delete")) method = "deleteFriend";
    else if (strstr(path, "is_friend")) method = "isFriend";
    miku_friend_handle_rpc(c->friend_svc, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_group(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getGroupInfo";
    if (strstr(path, "create")) method = "createGroup";
    else if (strstr(path, "invite")) method = "inviteToGroup";
    else if (strstr(path, "member")) method = "getGroupMemberList";
    miku_group_handle_rpc(c->group_svc, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_conv(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getAllConversations";
    if (strstr(path, "get_conv")) method = "getConversation";
    else if (strstr(path, "set")) method = "setConversation";
    miku_conv_handle_rpc(c->conv, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_msg(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "sendMsg";
    if (strstr(path, "get")) method = "getMsgByConv";
    else if (strstr(path, "revoke")) method = "revokeMsg";
    miku_msg_handle_rpc(c->msg, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_third(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getUploadToken";
    if (strstr(path, "download")) method = "getDownloadURL";
    miku_third_handle_rpc(c->third, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

int miku_api_register_routes(miku_http_server_t *srv, miku_api_ctx_t *ctx) {
    if (!srv || !ctx) return -1;
    miku_http_server_route(srv, "POST", "/auth/user_token", handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/auth/force_logout", handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/user/register", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_users_info", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/update_user_info", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/friend/add", handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/delete", handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_friend_list", handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/is_friend", handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/group/create", handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_info", handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/invite", handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_member_list", handle_group, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_all", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_conv", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/set", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/msg/send", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/revoke", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/third/upload_token", handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/download_url", handle_third, ctx);
    return 0;
}
