#include "miku_api.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_version.h"
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
    miku_stats_init(&ctx->stats, "miku-api", 0);
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
        miku_ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) { miku_jss(out, "token", token); miku_ji(out, "expireTimeSeconds", 86400); }
    } else if (strstr(path, "parse_token")) {
        const char *token = miku_json_str(miku_json_get(j, "token"));
        miku_ji(out, "errCode", 0);
        miku_jss(out, "userID", token ? "parsed_uid" : "");
        miku_jss(out, "platform", "linux");
    } else if (strstr(path, "admin_token")) {
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        const char *secret = miku_json_str(miku_json_get(j, "secret"));
        char token[512] = {0};
        int rc = miku_auth_user_token(c->auth, uid, secret, 5, token, sizeof(token));
        miku_ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) { miku_jss(out, "token", token); miku_ji(out, "expireTimeSeconds", 86400); }
    } else if (strstr(path, "force_logout_all")) {
        miku_ji(out, "errCode", 0);
    } else if (strstr(path, "force_logout")) {
        miku_ji(out, "errCode", 0);
    } else {
        miku_ji(out, "errCode", 404);
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
    else if (strstr(path, "account_check")) method = "accountCheck";
    else if (strstr(path, "get_all")) method = "getAllUsers";
    else if (strstr(path, "count")) method = "getUserCount";
    else if (strstr(path, "search")) method = "searchUser";
    else if (strstr(path, "online_status")) method = "getUsersOnlineStatus";
    else if (strstr(path, "global_recv")) method = "setGlobalRecvMessageOpt";
    else if (strstr(path, "user_status")) method = "updateUserStatus";
    else if (strstr(path, "process_user_command")) method = "processUserCommand";
    else if (strstr(path, "get_user_status")) method = "getUserStatus";
    else if (strstr(path, "get_subscribe_users")) method = "getSubscribeUsersStatus";
    else if (strstr(path, "subscribe_or_cancel")) method = "subscribeOrCancelUserStatus";
    else if (strstr(path, "set_user_status")) method = "setUserStatus";
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
    if (strstr(path, "add_black")) method = "addBlack";
    else if (strstr(path, "remove_black")) method = "removeBlack";
    else if (strstr(path, "get_black")) method = "getBlackList";
    else if (strstr(path, "add") && !strstr(path, "black")) method = "addFriend";
    else if (strstr(path, "delete") && !strstr(path, "friend")) method = "deleteFriend";
    else if (strstr(path, "is_friend")) method = "isFriend";
    else if (strstr(path, "delete_friend")) method = "deleteFriend";
    else if (strstr(path, "get_friend_apply")) method = "getFriendApplyList";
    else if (strstr(path, "get_self_apply")) method = "getSelfApplyList";
    else if (strstr(path, "get_designated")) method = "getDesignatedFriendApply";
    else if (strstr(path, "accept")) method = "acceptFriendApply";
    else if (strstr(path, "refuse")) method = "refuseFriendApply";
    else if (strstr(path, "import")) method = "importFriend";
    else if (strstr(path, "sync")) method = "syncFriend";
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
    else if (strstr(path, "member_list")) method = "getGroupMemberList";
    else if (strstr(path, "member") && strstr(path, "user_id")) method = "getGroupMemberUserID";
    else if (strstr(path, "member")) method = "getGroupMemberList";
    else if (strstr(path, "set_group_member")) method = "setGroupMemberInfo";
    else if (strstr(path, "set_group_info")) method = "setGroupInfo";
    else if (strstr(path, "get_groups_info")) method = "getGroupsInfo";
    else if (strstr(path, "get_group_info")) method = "getGroupInfo";
    else if (strstr(path, "joined")) method = "getJoinedGroupList";
    else if (strstr(path, "join")) method = "joinGroup";
    else if (strstr(path, "quit")) method = "quitGroup";
    else if (strstr(path, "dismiss")) method = "dismissGroup";
    else if (strstr(path, "mute") && strstr(path, "cancel")) method = "cancelMuteGroup";
    else if (strstr(path, "mute")) method = "muteGroup";
    else if (strstr(path, "kick")) method = "kickGroupMember";
    else if (strstr(path, "transfer")) method = "transferGroupOwner";
    else if (strstr(path, "applicant")) method = "getGroupApplicationList";
    else if (strstr(path, "accept")) method = "acceptGroupApplication";
    else if (strstr(path, "refuse")) method = "refuseGroupApplication";
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
    if (strstr(path, "get_all_conv")) method = "getAllConversations";
    else if (strstr(path, "get_conv")) method = "getConversation";
    else if (strstr(path, "get_conversation_list")) method = "getConversationList";
    else if (strstr(path, "get_conversations")) method = "getConversations";
    else if (strstr(path, "set_conv") && strstr(path, "min_seq")) method = "setConversationMinSeq";
    else if (strstr(path, "set_conv")) method = "setConversations";
    else if (strstr(path, "set")) method = "setConversation";
    else if (strstr(path, "total_unread")) method = "getTotalUnreadMsgCount";
    else if (strstr(path, "delete")) method = "deleteConversation";
    else if (strstr(path, "set_read")) method = "markConversationMessageAsRead";
    else if (strstr(path, "clear")) method = "clearConversationMsg";
    else if (strstr(path, "pin")) method = "pinConversation";
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
    if (strstr(path, "get_server")) method = "getServerTime";
    else if (strstr(path, "get_msg")) method = "getMsgByConv";
    else if (strstr(path, "get")) method = "getMsgByConv";
    else if (strstr(path, "revoke")) method = "revokeMsg";
    else if (strstr(path, "send_status")) method = "getSendMsgStatus";
    else if (strstr(path, "clean")) method = "cleanUpMsg";
    else if (strstr(path, "delete")) method = "deleteMsg";
    else if (strstr(path, "batch_send")) method = "batchSendMsg";
    else if (strstr(path, "send_msg")) method = "sendMsg";
    else if (strstr(path, "send")) method = "sendMsg";
    else if (strstr(path, "mark_as_read")) method = "markMsgAsRead";
    else if (strstr(path, "get_by_seq")) method = "getMsgBySeq";
    else if (strstr(path, "set_message_reaction")) method = "setMessageReactionExtensions";
    else if (strstr(path, "get_message_list_reaction")) method = "getMessageListReactionExtensions";
    else if (strstr(path, "add_message_reaction")) method = "addMessageReactionExtensions";
    else if (strstr(path, "delete_message_reaction")) method = "deleteMessageReactionExtensions";
    miku_msg_handle_rpc(c->msg, method, j, out);

    if (strcmp(method, "sendMsg") == 0) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *send_id = miku_json_str(miku_json_get(j, "sendID"));
            const char *recv_id = miku_json_str(miku_json_get(j, "recvID"));
            int64_t send_time = miku_json_int(miku_json_get(out, "sendTime"));
            if (send_id && recv_id) {
                miku_conversation_t conv;
                memset(&conv, 0, sizeof(conv));
                snprintf(conv.conversation_id, sizeof(conv.conversation_id),
                         "conv_%s_%s", send_id, recv_id);
                strncpy(conv.owner_user_id, send_id, sizeof(conv.owner_user_id) - 1);
                conv.conversation_type = 1;
                conv.latest_msg_send_time = send_time;
                miku_conv_handle_rpc(c->conv, "setConversation",
                                     miku_conversation_to_json(&conv),
                                     miku_json_create_object());
            }
        }
    }

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
    else if (strstr(path, "access")) method = "accessURL";
    else if (strstr(path, "delete")) method = "deleteObject";
    else if (strstr(path, "initiate_multipart")) method = "initiateMultipartUpload";
    else if (strstr(path, "complete_multipart")) method = "completeMultipartUpload";
    else if (strstr(path, "upload_info")) method = "getUploadInfo";
    else if (strstr(path, "object_info")) method = "getObjectInfo";
    else if (strstr(path, "signal_invitation")) method = "getSignalInvitationInfo";
    miku_third_handle_rpc(c->third, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_admin(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    if (strstr(path, "stats")) {
        miku_stats_snapshot_t snap;
        miku_stats_snapshot(&c->stats, &snap);
        miku_ji(out, "errCode", 0);
        miku_ji(out, "requestsTotal", snap.requests_total);
        miku_ji(out, "requestsFailed", snap.requests_failed);
        miku_ji(out, "connectionsActive", snap.connections_active);
        miku_ji(out, "connectionsTotal", snap.connections_total);
        miku_ji(out, "bytesSent", snap.bytes_sent);
        miku_ji(out, "bytesRecv", snap.bytes_recv);
        miku_ji(out, "uptimeMs", snap.uptime_ms);
        miku_jss(out, "service", snap.service_name);
    } else if (strstr(path, "health")) {
        miku_ji(out, "status", 0);
        miku_jss(out, "message", "ok");
    } else {
        miku_ji(out, "errCode", 404);
    }
    free(path);
    json_resp(resp, out);
}

static void handle_batch(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    if (strstr(path, "get_users_info")) {
        miku_json_val_t *uid_list = miku_json_get(j, "userIDList");
        miku_json_val_t *arr = miku_json_create_array();
        if (uid_list) {
            size_t n = miku_json_size(uid_list);
            for (size_t i = 0; i < n; i++) {
                const char *uid = miku_json_str(miku_json_at(uid_list, i));
                miku_json_val_t *get = miku_json_create_object();
                miku_json_object_set(get, "userID", miku_json_create_str(uid ? uid : ""));
                miku_json_val_t *r = miku_json_create_object();
                miku_user_handle_rpc(c->user, "getUserInfo", get, r);
                int64_t err = miku_json_int(miku_json_get(r, "errCode"));
                if (err == 0) {
                    miku_json_val_t *data = miku_json_get(r, "data");
                    if (data) miku_json_array_push(arr, data);
                }
                miku_json_destroy(get);
                miku_json_destroy(r);
            }
        }
        miku_ji(out, "errCode", 0);
        miku_json_object_set(out, "data", arr);
    } else if (strstr(path, "delete_friend")) {
        miku_json_val_t *r = miku_json_create_object();
        miku_friend_handle_rpc(c->friend_svc, "deleteFriend", j, r);
        miku_json_object_set(out, "errCode", miku_json_get(r, "errCode"));
        miku_json_destroy(r);
    } else {
        miku_ji(out, "errCode", 404);
    }
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_version(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req; (void)ctx;
    miku_json_val_t *out = miku_json_create_object();
    miku_jss(out, "version", MIKU_VERSION_STRING);
    miku_jss(out, "gitHash", MIKU_GIT_HASH);
    miku_jss(out, "buildDate", MIKU_BUILD_DATE);
    json_resp(resp, out);
}

int miku_api_register_routes(miku_http_server_t *srv, miku_api_ctx_t *ctx) {
    if (!srv || !ctx) return -1;

    /* Auth — 5 routes */
    miku_http_server_route(srv, "POST", "/auth/user_token",      handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/auth/parse_token",     handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/auth/admin_token",     handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/auth/force_logout",    handle_auth, ctx);
    miku_http_server_route(srv, "POST", "/auth/force_logout_all",handle_auth, ctx);

    /* User — 12 routes */
    miku_http_server_route(srv, "POST", "/user/register",                handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_users_info",          handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/update_user_info",        handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/account_check",           handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_all_users",           handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/count",                   handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/search",                  handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_users_online_status", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/set_global_recv_opt",     handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_global_recv_opt",     handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/update_user_status",      handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command",    handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_user_status",         handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_subscribe_users_status", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/subscribe_or_cancel_user_status", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/set_user_status",         handle_user, ctx);

    /* Friend — 14 routes */
    miku_http_server_route(srv, "POST", "/friend/add",                     handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/delete",                  handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_friend_list",         handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/is_friend",              handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/add_black",              handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/remove_black",           handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_black_list",         handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/delete_friend",          handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_friend_apply_list",  handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_self_apply_list",    handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_designated_apply",   handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/accept_apply",           handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/refuse_apply",           handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/import_friend",          handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/sync_friend",            handle_friend, ctx);

    /* Group — 22 routes */
    miku_http_server_route(srv, "POST", "/group/create",                      handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_info",              handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_groups_info",             handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/set_group_info",              handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_member_list",       handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_member_user_id",    handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/set_group_member_info",       handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/invite",                      handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/join",                        handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/quit",                        handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/dismiss",                     handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/mute",                        handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/cancel_mute",                 handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/kick",                        handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/transfer",                    handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_joined_group_list",       handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_applicant_list",    handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_application_list",  handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/accept_group_application",    handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/refuse_group_application",    handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/mute_member",                 handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/cancel_mute_member",          handle_group, ctx);

    /* Conversation — 12 routes */
    miku_http_server_route(srv, "POST", "/conversation/get_all",              handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_conv",             handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/set",                  handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_all_conversations",handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/set_conversations",    handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/delete_conversation",  handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_conversation_list",handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_conversations",    handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_total_unread",     handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/set_conversation_min_seq", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/mark_as_read",         handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/clear_conv_msg",       handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/pin_conversation",     handle_conv, ctx);

    /* Message — 12 routes */
    miku_http_server_route(srv, "POST", "/msg/send",            handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get",             handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/revoke",          handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/send_msg",        handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_msg",         handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_server_time", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_send_status", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/clean_up",        handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/delete_msg",      handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/batch_send",      handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/mark_as_read",    handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_by_seq",      handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/set_message_reaction_extensions", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_message_list_reaction_extensions", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/add_message_reaction_extensions", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/delete_message_reaction_extensions", handle_msg, ctx);

    /* Third/S3 — 7 routes */
    miku_http_server_route(srv, "POST", "/third/upload_token",            handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/download_url",            handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/access_url",              handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/delete_object",           handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/initiate_multipart",      handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/complete_multipart",      handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/get_upload_info",         handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/get_object_info",         handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/get_signal_invitation_info", handle_third, ctx);

    /* Batch operations — 2 routes */
    miku_http_server_route(srv, "POST", "/batch/get_users_info",          handle_batch, ctx);
    miku_http_server_route(srv, "POST", "/batch/delete_friend",           handle_batch, ctx);

    /* Admin — 3 routes + version */
    miku_http_server_route(srv, "POST", "/admin/stats",     handle_admin, ctx);
    miku_http_server_route(srv, "GET",  "/admin/health",    handle_admin, ctx);
    miku_http_server_route(srv, "POST", "/admin/shutdown",  handle_admin, ctx);
    miku_http_server_route(srv, "GET",  "/version",         handle_version, ctx);

    return 0;
}
