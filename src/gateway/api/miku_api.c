#include "miku_api.h"
#include "miku_log.h"
#include "miku_json.h"
#include "miku_json_util.h"
#include "miku_version.h"
#include "miku_msggw_ws_ops.h"
#include "miku_token.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "miku_hash.h"
#include <stdint.h>

typedef struct { const char *path; const char *method; } api_path_method_t;

static const api_path_method_t g_path_methods[] = {
    {"/user/register", "registerUser"},
    {"/user/get_users_info", "getUsersInfo"},
    {"/user/update_user_info", "updateUserInfo"},
    {"/user/account_check", "accountCheck"},
    {"/user/get_all_users", "getAllUsers"},
    {"/user/count", "getUserCount"},
    {"/user/search", "searchUser"},
    {"/user/get_users_online_status", "getUsersOnlineStatus"},
    {"/user/set_global_recv_opt", "setGlobalRecvMessageOpt"},
    {"/user/get_global_recv_opt", "getGlobalRecvMessageOpt"},
    {"/user/update_user_status", "updateUserStatus"},
    {"/user/process_user_command", "processUserCommand"},
    {"/user/get_user_status", "getUserStatus"},
    {"/user/get_subscribe_users_status", "getSubscribeUsersStatus"},
    {"/user/subscribe_or_cancel_user_status", "subscribeOrCancelUserStatus"},
    {"/user/set_user_status", "setUserStatus"},
    {"/user/update_user_info_ex", "updateUserInfoEx"},
    {"/user/get_all_users_uid", "getAllUsersUID"},
    {"/user/get_users", "getUsersInfo"},
    {"/user/get_users_online_token_detail", "getUsersOnlineTokenDetail"},
    {"/user/process_user_command_add", "processUserCommandAdd"},
    {"/user/process_user_command_delete", "processUserCommandDelete"},
    {"/user/process_user_command_update", "processUserCommandUpdate"},
    {"/user/process_user_command_get", "processUserCommandGet"},
    {"/user/process_user_command_get_all", "processUserCommandGetAll"},
    {"/user/add_notification_account", "addNotificationAccount"},
    {"/user/update_notification_account", "updateNotificationAccount"},
    {"/user/search_notification_account", "searchNotificationAccount"},
    {"/user/get_user_client_config", "getUserClientConfig"},
    {"/user/set_user_client_config", "setUserClientConfig"},
    {"/user/del_user_client_config", "delUserClientConfig"},
    {"/user/page_user_client_config", "pageUserClientConfig"},
    {"/friend/add", "addFriend"},
    {"/friend/delete", "deleteFriend"},
    {"/friend/get_friend_list", "getFriendList"},
    {"/friend/is_friend", "isFriend"},
    {"/friend/add_black", "addBlack"},
    {"/friend/remove_black", "removeBlack"},
    {"/friend/get_black_list", "getBlackList"},
    {"/friend/delete_friend", "deleteFriend"},
    {"/friend/get_friend_apply_list", "getFriendApplyList"},
    {"/friend/get_self_apply_list", "getSelfApplyList"},
    {"/friend/get_designated_apply", "getDesignatedFriendApply"},
    {"/friend/accept_apply", "acceptFriendApply"},
    {"/friend/refuse_apply", "refuseFriendApply"},
    {"/friend/import_friend", "importFriend"},
    {"/friend/sync_friend", "syncFriend"},
    {"/friend/add_friend_response", "respondFriendApply"},
    {"/friend/set_friend_remark", "setFriendRemark"},
    {"/friend/get_designated_friends", "getDesignatedFriends"},
    {"/friend/get_specified_blacks", "getSpecifiedBlacks"},
    {"/friend/get_incremental_blacks", "getIncrementalBlacks"},
    {"/friend/get_incremental_friends", "getIncrementalFriends"},
    {"/friend/get_friend_id", "getFriendIDs"},
    {"/friend/get_specified_friends_info", "getSpecifiedFriendsInfo"},
    {"/friend/update_friends", "updateFriends"},
    {"/friend/get_full_friend_user_ids", "getFullFriendUserIDs"},
    {"/friend/get_self_unhandled_apply_count", "getSelfUnhandledApplyCount"},
    {"/group/create", "createGroup"},
    {"/group/get_group_info", "getGroupInfo"},
    {"/group/get_groups_info", "getGroupsInfo"},
    {"/group/set_group_info", "setGroupInfo"},
    {"/group/get_group_member_list", "getGroupMemberList"},
    {"/group/get_group_member_user_id", "getGroupMemberUserID"},
    {"/group/set_group_member_info", "setGroupMemberInfo"},
    {"/group/invite", "inviteToGroup"},
    {"/group/join", "joinGroup"},
    {"/group/quit", "quitGroup"},
    {"/group/dismiss", "dismissGroup"},
    {"/group/mute", "muteGroup"},
    {"/group/cancel_mute", "cancelMuteGroup"},
    {"/group/kick", "kickGroupMember"},
    {"/group/transfer", "transferGroupOwner"},
    {"/group/get_joined_group_list", "getJoinedGroupList"},
    {"/group/get_group_applicant_list", "getGroupApplicationList"},
    {"/group/get_group_application_list", "getGroupApplicationList"},
    {"/group/accept_group_application", "acceptGroupApplication"},
    {"/group/refuse_group_application", "refuseGroupApplication"},
    {"/group/mute_member", "muteGroupMember"},
    {"/group/cancel_mute_member", "cancelMuteGroupMember"},
    {"/group/set_group_info_ex", "setGroupInfoEx"},
    {"/group/get_recv_group_applicationList", "getRecvGroupApplicationList"},
    {"/group/get_user_req_group_applicationList", "getUserReqGroupApplicationList"},
    {"/group/get_group_users_req_application_list", "getGroupUsersReqApplicationList"},
    {"/group/get_specified_user_group_request_info", "getSpecifiedUserGroupRequestInfo"},
    {"/group/get_group_abstract_info", "getGroupAbstractInfo"},
    {"/group/get_groups", "getGroups"},
    {"/group/get_incremental_join_groups", "getIncrementalJoinGroups"},
    {"/group/get_incremental_group_members", "getIncrementalGroupMembers"},
    {"/group/get_incremental_group_members_batch", "getIncrementalGroupMemberBatch"},
    {"/group/get_full_group_member_user_ids", "getFullGroupMemberUserIDs"},
    {"/group/get_full_join_group_ids", "getFullJoinGroupIDs"},
    {"/group/get_group_application_unhandled_count", "getGroupApplicationUnhandledCount"},
    {"/conversation/get_all", "getAllConversations"},
    {"/conversation/get_conv", "getConversation"},
    {"/conversation/set", "setConversation"},
    {"/conversation/get_all_conversations", "getAllConversations"},
    {"/conversation/set_conversations", "setConversations"},
    {"/conversation/delete_conversation", "deleteConversation"},
    {"/conversation/get_conversation_list", "getConversationList"},
    {"/conversation/get_conversations", "getConversations"},
    {"/conversation/get_total_unread", "getTotalUnreadMsgCount"},
    {"/conversation/set_conversation_min_seq", "setConversationMinSeq"},
    {"/conversation/mark_as_read", "markConversationMessageAsRead"},
    {"/conversation/clear_conv_msg", "clearConversationMsg"},
    {"/conversation/pin_conversation", "pinConversation"},
    {"/conversation/get_sorted_conversation_list", "getSortedConversationList"},
    {"/conversation/get_full_conversation_ids", "getFullConversationIDs"},
    {"/conversation/get_incremental_conversations", "getIncrementalConversation"},
    {"/conversation/get_owner_conversation", "getOwnerConversation"},
    {"/conversation/get_not_notify_conversation_ids", "getNotNotifyConversationIDs"},
    {"/conversation/get_pinned_conversation_ids", "getPinnedConversationIDs"},
    {"/conversation/delete_conversations", "deleteConversations"},
    {"/conversation/update_conversations_by_user", "updateConversationsByUser"},
    {"/msg/send", "send"},
    {"/msg/get", "getMsgByConv"},
    {"/msg/revoke", "revokeMsg"},
    {"/msg/send_msg", "sendMsg"},
    {"/msg/get_msg", "getMsg"},
    {"/msg/get_server_time", "getServerTime"},
    {"/msg/get_send_status", "getSendMsgStatus"},
    {"/msg/clean_up", "cleanUpMsg"},
    {"/msg/delete_msg", "deleteMsg"},
    {"/msg/batch_send", "batchSendMsg"},
    {"/msg/mark_as_read", "markMsgAsRead"},
    {"/msg/get_by_seq", "getMsgBySeq"},
    {"/msg/set_message_reaction_extensions", "setMessageReactionExtensions"},
    {"/msg/get_message_list_reaction_extensions", "getMessageListReactionExtensions"},
    {"/msg/add_message_reaction_extensions", "addMessageReactionExtensions"},
    {"/msg/delete_message_reaction_extensions", "deleteMessageReactionExtensions"},
    {"/msg/newest_seq", "getNewestSeq"},
    {"/msg/search_msg", "searchMsg"},
    {"/msg/send_business_notification", "sendBusinessNotification"},
    {"/msg/pull_msg_by_seq", "pullMsgBySeq"},
    {"/msg/mark_msgs_as_read", "markMsgsAsRead"},
    {"/msg/mark_conversation_as_read", "markConversationAsRead"},
    {"/msg/get_conversations_has_read_and_max_seq", "getConversationsHasReadAndMaxSeq"},
    {"/msg/set_conversation_has_read_seq", "setConversationHasReadSeq"},
    {"/msg/clear_conversation_msg", "clearConversationMsg"},
    {"/msg/user_clear_all_msg", "userClearAllMsg"},
    {"/msg/delete_msg_phsical_by_seq", "deleteMsgPhysicalBySeq"},
    {"/msg/delete_msg_physical", "deleteMsgPhysical"},
    {"/msg/send_simple_msg", "sendSimpleMsg"},
    {"/msg/check_msg_is_send_success", "checkMsgIsSendSuccess"},
    {"/third/upload_token", "getUploadToken"},
    {"/third/download_url", "getDownloadURL"},
    {"/third/access_url", "accessURL"},
    {"/third/delete_object", "deleteObject"},
    {"/third/initiate_multipart", "initiateMultipartUpload"},
    {"/third/complete_multipart", "completeMultipartUpload"},
    {"/third/get_upload_info", "getUploadInfo"},
    {"/third/get_object_info", "getObjectInfo"},
    {"/third/get_signal_invitation_info", "getSignalInvitationInfo"},
    {"/third/fcm_update_token", "fcmUpdateToken"},
    {"/third/set_app_badge", "setAppBadge"},
    {"/third/logs/upload", "uploadLogs"},
    {"/third/logs/delete", "deleteLogs"},
    {"/third/logs/search", "searchLogs"},
    {"/third/prometheus", "getPrometheus"},
    {"/object/part_limit", "partLimit"},
    {"/object/part_size", "partSize"},
    {"/object/auth_sign", "authSign"},
    {"/object/initiate_form_data", "initiateFormData"},
    {"/object/complete_form_data", "completeFormData"},
    {"/object/access_url", "accessURL"},
    {"/object/initiate_multipart_upload", "initiateMultipartUpload"},
    {"/object/complete_multipart_upload", "completeMultipartUpload"},
};

#define MK_API_PATH_HASH 512
static int16_t g_path_hash[MK_API_PATH_HASH];
static int g_path_hash_ready;

static void api_path_hash_init(void) {
    if (g_path_hash_ready) return;
    for (int i = 0; i < MK_API_PATH_HASH; i++) g_path_hash[i] = -1;
    for (int i = 0; i < (int)(sizeof(g_path_methods) / sizeof(g_path_methods[0])); i++) {
        const char *p = g_path_methods[i].path;
        uint32_t idx = (uint32_t)(miku_fnv1a_64(p, strlen(p)) & (MK_API_PATH_HASH - 1));
        for (int n = 0; n < MK_API_PATH_HASH; n++) {
            if (g_path_hash[idx] < 0) { g_path_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_API_PATH_HASH - 1);
        }
    }
    g_path_hash_ready = 1;
}

static void api_req_path(const miku_http_request_t *req, char *out, size_t cap) {
    size_t n = req && req->path.data ? req->path.len : 0;
    if (n >= cap) n = cap - 1;
    if (n && req->path.data) memcpy(out, req->path.data, n);
    out[n] = '\0';
}

static const char *api_rpc_method(const miku_http_request_t *req, const char *fallback) {
    char path[256];
    api_req_path(req, path, sizeof(path));
    api_path_hash_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(path, strlen(path)) & (MK_API_PATH_HASH - 1));
    for (int n = 0; n < MK_API_PATH_HASH; n++) {
        int pi = g_path_hash[idx];
        if (pi < 0) break;
        if (strcmp(g_path_methods[pi].path, path) == 0)
            return g_path_methods[pi].method;
        idx = (idx + 1) & (MK_API_PATH_HASH - 1);
    }
    return fallback;
}


miku_api_ctx_t *miku_api_ctx_create(void) {
    miku_api_ctx_t *ctx = (miku_api_ctx_t *)calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;
    ctx->auth = miku_auth_service_create();
    ctx->user = miku_user_service_create();
    ctx->friend_svc = miku_friend_service_create();
    ctx->group_svc = miku_group_service_create();
    ctx->conv = miku_conv_service_create();
    ctx->msg = miku_msg_service_create();
    miku_msg_service_set_group_svc(ctx->msg, ctx->group_svc);
    miku_msg_service_set_friend_svc(ctx->msg, ctx->friend_svc);
    ctx->third = miku_third_service_create();
    ctx->ratelimit = miku_ratelimit_create(60000, 100);
    ctx->webhook = miku_webhook_create();
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
    miku_ratelimit_destroy(ctx->ratelimit);
    miku_webhook_destroy(ctx->webhook);
    free(ctx);
}

static void upsert_conv_on_send(miku_conv_service_t *svc, const char *owner,
                                const char *cid, int conv_type,
                                const char *peer_user_id, const char *group_id,
                                int64_t send_time, const char *content,
                                int bump_unread) {
    miku_conv_touch_on_send(svc, owner, cid, conv_type, peer_user_id, group_id,
                            send_time, content, bump_unread);
}

typedef struct {
    miku_conv_service_t *svc;
    const char          *cid;
    const char          *gid;
    const char          *send_id;
    int64_t              send_time;
    const char          *content;
} group_conv_upsert_ctx_t;

static void upsert_group_member_conv(const char *user_id, int role, void *v) {
    (void)role;
    group_conv_upsert_ctx_t *g = (group_conv_upsert_ctx_t *)v;
    if (!g || !user_id) return;
    int bump = (g->send_id && strcmp(user_id, g->send_id) != 0) ? 1 : 0;
    upsert_conv_on_send(g->svc, user_id, g->cid, MK_IM_CONV_GROUP,
                        NULL, g->gid, g->send_time, g->content, bump);
}

static void json_resp(miku_http_response_t *resp, miku_json_val_t *j) {
    miku_string_t *s = miku_json_stringify(j);
    miku_http_response_set_json(resp, s->data);
    miku_str_destroy(s);
    miku_json_destroy(j);
}

static int check_ratelimit(miku_api_ctx_t *c, miku_http_request_t *req, miku_http_response_t *resp) {
    if (!c->ratelimit) return 0;
    /* Prefer token-derived user id (no extra JSON parse). Fall back to body fields. */
    char key_buf[128];
    snprintf(key_buf, sizeof(key_buf), "global");
    const char *token = NULL;
    if (req->headers) {
        token = (const char *)miku_hashmap_get(req->headers, "token");
        if (!token) token = (const char *)miku_hashmap_get(req->headers, "authorization");
    }
    if (token && token[0]) {
        if (strncmp(token, "Bearer ", 7) == 0) token += 7;
        char uid[128] = {0};
        if (miku_auth_parse_token(c->auth, token, uid, sizeof(uid)) == 0 && uid[0])
            snprintf(key_buf, sizeof(key_buf), "%s", uid);
    }
    if (!miku_ratelimit_allow(c->ratelimit, key_buf)) {
        resp->status = 429;
        miku_json_val_t *body = miku_json_create_object();
        miku_ji(body, "errCode", 429);
        miku_jss(body, "errMsg", "rate limit exceeded");
        json_resp(resp, body);
        return -1;
    }
    return 0;
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

static int require_fields(miku_json_val_t *j, miku_http_response_t *resp, ...) {
    va_list ap;
    va_start(ap, resp);
    const char *field;
    int missing = 0;
    char missing_names[512] = {0};
    size_t pos = 0;
    while ((field = va_arg(ap, const char *)) != NULL) {
        miku_json_val_t *v = miku_json_get(j, field);
        if (!v || (v->type == MK_JSON_STRING && (!v->u.str_val || v->u.str_val[0] == '\0'))) {
            if (pos > 0 && pos < sizeof(missing_names) - 2) missing_names[pos++] = ',';
            size_t flen = strlen(field);
            if (pos + flen < sizeof(missing_names) - 1) {
                memcpy(missing_names + pos, field, flen);
                pos += flen;
            }
            missing++;
        }
    }
    va_end(ap);
    if (missing > 0) {
        miku_json_val_t *body = miku_json_create_object();
        miku_ji(body, "errCode", 400);
        char msg[600];
        snprintf(msg, sizeof(msg), "missing required fields: %s", missing_names);
        miku_jss(body, "errMsg", msg);
        resp->status = 400;
        json_resp(resp, body);
        return -1;
    }
    return 0;
}

static int verify_token(miku_api_ctx_t *c, miku_http_request_t *req, miku_http_response_t *resp) {
    if (!c->auth) return 0;
    if (req->path.data && req->path.len > 0) {
        /* Public: token issuance + health/version/metrics scrape */
        if (req->path.len == 16 && strncmp(req->path.data, "/auth/user_token", 16) == 0) return 0;
        if (req->path.len == 17 && strncmp(req->path.data, "/auth/admin_token", 17) == 0) return 0;
        if (req->path.len == 13 && strncmp(req->path.data, "/admin/health", 13) == 0) return 0;
        if (req->path.len == 8 && strncmp(req->path.data, "/version", 8) == 0) return 0;
    }
    const char *token = NULL;
    if (req->headers) token = (const char *)miku_hashmap_get(req->headers, "token");
    if (!token && req->headers) token = (const char *)miku_hashmap_get(req->headers, "authorization");
    if (token && strncmp(token, "Bearer ", 7) == 0) token += 7;
    if (!token || !token[0]) {
        miku_json_val_t *body = miku_json_create_object();
        miku_ji(body, "errCode", 401);
        miku_jss(body, "errMsg", "missing token header");
        resp->status = 401;
        json_resp(resp, body);
        return -1;
    }
    char uid[128] = {0};
    if (miku_auth_parse_token(c->auth, token, uid, sizeof(uid)) != 0) {
        miku_json_val_t *body = miku_json_create_object();
        miku_ji(body, "errCode", 401);
        miku_jss(body, "errMsg", "invalid token");
        resp->status = 401;
        json_resp(resp, body);
        return -1;
    }
    return 0;
}

/* Fill uid from token header; returns 0 on success. */
static int req_token_uid(miku_api_ctx_t *c, miku_http_request_t *req, char *uid, size_t cap) {
    if (!uid || cap == 0) return -1;
    uid[0] = '\0';
    if (!c || !c->auth || !req || !req->headers) return -1;
    const char *token = (const char *)miku_hashmap_get(req->headers, "token");
    if (!token) token = (const char *)miku_hashmap_get(req->headers, "authorization");
    if (token && strncmp(token, "Bearer ", 7) == 0) token += 7;
    if (!token || !token[0]) return -1;
    return miku_auth_parse_token(c->auth, token, uid, cap);
}

/* Return token platformID, or -1 on failure. Admin tokens use platform 5. */
static int req_token_platform(miku_http_request_t *req) {
    if (!req || !req->headers) return -1;
    const char *token = (const char *)miku_hashmap_get(req->headers, "token");
    if (!token) token = (const char *)miku_hashmap_get(req->headers, "authorization");
    if (token && strncmp(token, "Bearer ", 7) == 0) token += 7;
    if (!token || !token[0]) return -1;
    char uid[128] = {0};
    int plat = -1;
    if (miku_token_verify_ex(token, miku_token_default_secret(), uid, sizeof(uid),
                             &plat, NULL) != 0)
        return -1;
    return plat;
}

static int api_may_access_conv(miku_api_ctx_t *c, const char *uid, const char *conv) {
    if (!uid || !uid[0] || !conv || !conv[0]) return 0;
    if (strncmp(conv, "si_", 3) == 0)
        return miku_friend_may_access_si_conv(c ? c->friend_svc : NULL, uid, conv);
    if (strncmp(conv, "sg_", 3) == 0) {
        if (!c || !c->group_svc) return 0;
        return miku_group_is_member(c->group_svc, conv + 3, uid);
    }
    if (c && c->conv) {
        miku_conversation_t cv;
        return miku_conv_get(c->conv, uid, conv, &cv) == 0;
    }
    return 0;
}

/* Non-admin may only view self or mutual-friend profiles. plat: token platform (-1 unknown). */
static int api_may_view_user(miku_api_ctx_t *c, int plat,
                             const char *actor, const char *uid) {
    if (!uid || !uid[0]) return 0;
    if (plat == 5) return 1;
    if (!actor || !actor[0]) return 0;
    if (strcmp(actor, uid) == 0) return 1;
    return miku_friend_is_mutual(c->friend_svc, actor, uid);
}

static int conv_mutates(const char *method) {
    return strcmp(method, "setConversation") == 0
        || strcmp(method, "setConversations") == 0
        || strcmp(method, "deleteConversation") == 0
        || strcmp(method, "deleteConversations") == 0
        || strcmp(method, "pinConversation") == 0
        || strcmp(method, "setConversationMinSeq") == 0
        || strcmp(method, "markConversationMessageAsRead") == 0
        || strcmp(method, "clearConversationMsg") == 0
        || strcmp(method, "updateConversationsByUser") == 0;
}

static int api_conv_ids_denied(miku_api_ctx_t *c, const char *actor, miku_json_val_t *j) {
    if (!actor || !actor[0] || !j) return 0;
    const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
    if (cid && cid[0] && !api_may_access_conv(c, actor, cid)) return 1;
    miku_json_val_t *ids = miku_json_get(j, "conversationIDs");
    if (!ids) ids = miku_json_get(j, "conversationIDList");
    if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
        size_t n = miku_json_size(ids);
        for (size_t i = 0; i < n; i++) {
            const char *cidi = miku_json_str(miku_json_at(ids, i));
            if (cidi && cidi[0] && !api_may_access_conv(c, actor, cidi)) return 1;
        }
    }
    return 0;
}

static int conv_is_read(const char *method) {
    return strcmp(method, "getAllConversations") == 0
        || strcmp(method, "getConversation") == 0
        || strcmp(method, "getConversationList") == 0
        || strcmp(method, "getConversations") == 0
        || strcmp(method, "getActiveConversations") == 0
        || strcmp(method, "getTotalUnreadMsgCount") == 0
        || strcmp(method, "getSortedConversationList") == 0
        || strcmp(method, "getFullConversationIDs") == 0
        || strcmp(method, "getConversationsHasReadAndMaxSeq") == 0
        || strcmp(method, "getOwnerConversation") == 0
        || strcmp(method, "getPinnedConversationIDs") == 0
        || strcmp(method, "getNotNotifyConversationIDs") == 0
        || strcmp(method, "getIncrementalConversation") == 0;
}

static void filter_conv_unread_count(miku_api_ctx_t *c, const char *actor, miku_json_val_t *out) {
    if (!c || !c->conv || !actor || !actor[0] || !out) return;
    miku_conversation_t convs[512];
    int n = miku_conv_get_all(c->conv, actor, convs, 512);
    int64_t total = 0;
    for (int i = 0; i < n; i++) {
        if (api_may_access_conv(c, actor, convs[i].conversation_id))
            total += convs[i].unread_count;
    }
    miku_ji(out, "count", (int)total);
}

/* Compact data[] in place; keep_fn returns 1 to retain item (ownership stays in array). */
static void filter_json_array_inplace(miku_json_val_t *arr,
                                      int (*keep_fn)(miku_json_val_t *item, void *ctx),
                                      void *ctx) {
    if (!arr || miku_json_type(arr) != MK_JSON_ARRAY || !keep_fn) return;
    size_t n = miku_json_size(arr);
    size_t w = 0;
    for (size_t i = 0; i < n; i++) {
        miku_json_val_t *item = miku_json_at(arr, i);
        if (!keep_fn(item, ctx)) {
            miku_json_destroy(item);
            arr->u.array.items[i] = NULL;
            continue;
        }
        if (w != i) arr->u.array.items[w] = item;
        w++;
    }
    arr->u.array.count = w;
}

typedef struct {
    miku_api_ctx_t *c;
    const char     *actor;
} conv_filter_ctx_t;

static int conv_filter_keep_object(miku_json_val_t *item, void *v) {
    conv_filter_ctx_t *f = (conv_filter_ctx_t *)v;
    const char *cid = miku_json_str(miku_json_get(item, "conversationID"));
    return cid && api_may_access_conv(f->c, f->actor, cid);
}

static int conv_filter_keep_id_str(miku_json_val_t *item, void *v) {
    conv_filter_ctx_t *f = (conv_filter_ctx_t *)v;
    const char *cid = miku_json_str(item);
    return cid && api_may_access_conv(f->c, f->actor, cid);
}

static int msg_filter_keep(miku_json_val_t *item, void *v) {
    conv_filter_ctx_t *f = (conv_filter_ctx_t *)v;
    const char *cid = miku_json_str(miku_json_get(item, "conversationID"));
    return cid && api_may_access_conv(f->c, f->actor, cid);
}

static void filter_msg_read_result(miku_api_ctx_t *c, const char *actor, miku_json_val_t *out) {
    if (!c || !actor || !actor[0] || !out) return;
    miku_json_val_t *data = miku_json_get(out, "data");
    if (!data || miku_json_type(data) != MK_JSON_ARRAY) return;
    conv_filter_ctx_t fctx = { .c = c, .actor = actor };
    filter_json_array_inplace(data, msg_filter_keep, &fctx);
}

typedef struct {
    miku_api_ctx_t *c;
    int             plat;
    const char     *actor;
    const char     *keyword; /* searchUser: exact userID match */
} user_filter_ctx_t;

static int user_filter_keep(miku_json_val_t *item, void *v) {
    user_filter_ctx_t *f = (user_filter_ctx_t *)v;
    const char *uid = miku_json_str(miku_json_get(item, "userID"));
    if (!uid || !api_may_view_user(f->c, f->plat, f->actor, uid)) return 0;
    if (f->keyword && strcmp(uid, f->keyword) != 0) return 0;
    return 1;
}

static void filter_users_read_result(miku_api_ctx_t *c, int plat, const char *actor,
                                     const char *keyword, miku_json_val_t *out) {
    if (!c || !actor || !actor[0] || !out) return;
    miku_json_val_t *data = miku_json_get(out, "data");
    if (!data || miku_json_type(data) != MK_JSON_ARRAY) return;
    user_filter_ctx_t fctx = { .c = c, .plat = plat, .actor = actor, .keyword = keyword };
    filter_json_array_inplace(data, user_filter_keep, &fctx);
}

static int group_filter_keep_object(miku_json_val_t *item, void *v) {
    conv_filter_ctx_t *f = (conv_filter_ctx_t *)v;
    const char *gid = miku_json_str(miku_json_get(item, "groupID"));
    return gid && f->actor && f->actor[0]
        && miku_group_is_member(f->c->group_svc, gid, f->actor);
}

static void filter_group_id_list(miku_api_ctx_t *c, const char *actor, miku_json_val_t *j) {
    if (!c || !actor || !actor[0] || !j) return;
    miku_json_val_t *ids = miku_json_get(j, "groupIDList");
    if (!ids || miku_json_type(ids) != MK_JSON_ARRAY) return;
    miku_json_val_t *filtered = miku_json_create_array();
    size_t n = miku_json_size(ids);
    for (size_t i = 0; i < n; i++) {
        const char *gid = miku_json_str(miku_json_at(ids, i));
        if (gid && miku_group_is_member(c->group_svc, gid, actor))
            miku_json_array_push(filtered, miku_json_create_str(gid));
    }
    miku_json_object_set(j, "groupIDList", filtered);
}

static void filter_groups_read_result(miku_api_ctx_t *c, const char *actor, miku_json_val_t *out) {
    if (!c || !actor || !actor[0] || !out) return;
    miku_json_val_t *data = miku_json_get(out, "data");
    if (!data || miku_json_type(data) != MK_JSON_ARRAY) return;
    conv_filter_ctx_t fctx = { .c = c, .actor = actor };
    filter_json_array_inplace(data, group_filter_keep_object, &fctx);
}

static void filter_conv_read_result(miku_api_ctx_t *c, const char *actor, miku_json_val_t *out) {
    if (!c || !actor || !actor[0] || !out) return;
    miku_json_val_t *data = miku_json_get(out, "data");
    if (!data) return;
    conv_filter_ctx_t fctx = { .c = c, .actor = actor };
    if (miku_json_type(data) == MK_JSON_ARRAY) {
        size_t n = miku_json_size(data);
        if (n > 0 && miku_json_type(miku_json_at(data, 0)) == MK_JSON_STRING)
            filter_json_array_inplace(data, conv_filter_keep_id_str, &fctx);
        else
            filter_json_array_inplace(data, conv_filter_keep_object, &fctx);
    } else if (miku_json_type(data) == MK_JSON_OBJECT) {
        const char *cid = miku_json_str(miku_json_get(data, "conversationID"));
        if (!cid || !api_may_access_conv(c, actor, cid)) {
            miku_ji(out, "errCode", 0);
            miku_json_object_set(out, "data", miku_json_create_null());
        }
    }
}

static void filter_presence_user_id_list(miku_api_ctx_t *c, int plat,
                                         const char *actor, miku_json_val_t *j,
                                         const char *key) {
    if (!j || !key || plat == 5) return;
    miku_json_val_t *list = miku_json_get(j, key);
    if (!list || miku_json_type(list) != MK_JSON_ARRAY) return;
    miku_json_val_t *filtered = miku_json_create_array();
    size_t n = miku_json_size(list);
    for (size_t i = 0; i < n; i++) {
        const char *uid = miku_json_str(miku_json_at(list, i));
        if (api_may_view_user(c, plat, actor, uid))
            miku_json_array_push(filtered, miku_json_create_str(uid ? uid : ""));
    }
    miku_json_object_set(j, key, filtered);
}

static void api_drop_group_conv(miku_conv_service_t *conv, const char *uid, const char *gid) {
    if (!conv || !uid || !uid[0] || !gid || !gid[0]) return;
    char cid[MK_CONV_ID_LEN];
    snprintf(cid, sizeof(cid), "sg_%s", gid);
    miku_conv_delete(conv, uid, cid);
}

/* Keep only inviter + mutual friends in invitee lists (block force-add strangers). */
static void filter_group_invitee_ids(miku_api_ctx_t *c, const char *from,
                                    miku_json_val_t *j) {
    if (!c || !from || !from[0] || !j) return;
    static const char *keys[] = {"memberUserIDs", "invitedUserIDs"};
    for (size_t k = 0; k < sizeof(keys) / sizeof(keys[0]); k++) {
        miku_json_val_t *ids = miku_json_get(j, keys[k]);
        if (!ids || miku_json_type(ids) != MK_JSON_ARRAY) continue;
        miku_json_val_t *filtered = miku_json_create_array();
        size_t n = miku_json_size(ids);
        for (size_t i = 0; i < n; i++) {
            const char *u = miku_json_str(miku_json_at(ids, i));
            if (!u || !u[0]) continue;
            if (strcmp(u, from) == 0
                || miku_friend_is_mutual(c->friend_svc, from, u))
                miku_json_array_push(filtered, miku_json_create_str(u));
        }
        miku_json_object_set(j, keys[k], filtered);
    }
    const char *uid = miku_json_str(miku_json_get(j, "userID"));
    if (uid && uid[0] && strcmp(uid, from) != 0
        && !miku_friend_is_mutual(c->friend_svc, from, uid))
        miku_jss(j, "userID", "");
}

static void handle_auth(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char path[128];
    api_req_path(req, path, sizeof(path));
    if (strcmp(path, "/auth/user_token") == 0) {
        if (require_fields(j, resp, "userID", "secret", (const char *)NULL)) { miku_json_destroy(j); return; }
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        const char *secret = miku_json_str(miku_json_get(j, "secret"));
        int64_t plat = miku_json_int(miku_json_get(j, "platformID"));
        char token[512] = {0};
        int rc = miku_auth_user_token(c->auth, uid, secret, (int)plat, token, sizeof(token));
        miku_ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) { miku_jss(out, "token", token); miku_ji(out, "expireTimeSeconds", 86400); }
    } else if (strcmp(path, "/auth/parse_token") == 0) {
        const char *token = miku_json_str(miku_json_get(j, "token"));
        char actor[128] = {0};
        int plat = req_token_platform(req);
        req_token_uid(c, req, actor, sizeof(actor));
        char uid[128] = {0};
        int rc = (token && token[0]) ? miku_auth_parse_token(c->auth, token, uid, sizeof(uid)) : -1;
        if (rc == 0 && plat != 5 && (!actor[0] || strcmp(uid, actor) != 0))
            rc = -1;
        miku_ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) {
            miku_jss(out, "userID", uid);
            miku_jss(out, "errMsg", "");
        } else {
            miku_jss(out, "errMsg", "invalid token");
        }
    } else if (strcmp(path, "/auth/admin_token") == 0) {
        if (require_fields(j, resp, "userID", "secret", (const char *)NULL)) { miku_json_destroy(j); return; }
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        const char *secret = miku_json_str(miku_json_get(j, "secret"));
        char token[512] = {0};
        int rc = miku_auth_admin_token(c->auth, uid, secret, token, sizeof(token));
        miku_ji(out, "errCode", rc == 0 ? 0 : 401);
        if (rc == 0) { miku_jss(out, "token", token); miku_ji(out, "expireTimeSeconds", 86400); }
    } else if (strcmp(path, "/auth/force_logout_all") == 0) {
        char actor[128] = {0};
        if (req_token_uid(c, req, actor, sizeof(actor)) != 0 || !actor[0]) {
            miku_ji(out, "errCode", 401);
            miku_jss(out, "errMsg", "invalid token");
        } else {
            int rc = miku_auth_force_logout(c->auth, actor, -1);
            if (rc == 0 && c->on_kick) c->on_kick(actor, -1, c->on_kick_ctx);
            miku_ji(out, "errCode", rc == 0 ? 0 : 500);
        }
    } else if (strcmp(path, "/auth/force_logout") == 0) {
        char actor[128] = {0};
        if (req_token_uid(c, req, actor, sizeof(actor)) != 0 || !actor[0]) {
            miku_ji(out, "errCode", 401);
            miku_jss(out, "errMsg", "invalid token");
        } else {
            int plat = (int)miku_json_int(miku_json_get(j, "platformID"));
            int rc = miku_auth_force_logout(c->auth, actor, plat);
            if (rc == 0 && c->on_kick) c->on_kick(actor, plat, c->on_kick_ctx);
            miku_ji(out, "errCode", rc == 0 ? 0 : 500);
        }
    } else {
        miku_ji(out, "errCode", 404);
    }
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_user(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "getUserInfo");
    char actor[128] = {0};
    int plat = req_token_platform(req);
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        if (strcmp(method, "updateUserInfo") == 0 || strcmp(method, "updateUserInfoEx") == 0
            || strcmp(method, "setGlobalRecvMessageOpt") == 0
            || strcmp(method, "getGlobalRecvMessageOpt") == 0
            || strcmp(method, "updateUserStatus") == 0 || strcmp(method, "setUserStatus") == 0
            || strcmp(method, "setUserClientConfig") == 0
            || strcmp(method, "getUserClientConfig") == 0
            || strcmp(method, "delUserClientConfig") == 0
            || strcmp(method, "pageUserClientConfig") == 0
            || strcmp(method, "processUserCommand") == 0
            || strcmp(method, "processUserCommandAdd") == 0
            || strcmp(method, "processUserCommandDelete") == 0
            || strcmp(method, "processUserCommandUpdate") == 0
            || strcmp(method, "processUserCommandGet") == 0
            || strcmp(method, "processUserCommandGetAll") == 0)
            miku_jss(j, "userID", actor);
        else if (strcmp(method, "registerUser") == 0 && plat != 5)
            miku_jss(j, "userID", actor);
        else if (strcmp(method, "subscribeOrCancelUserStatus") == 0)
            miku_jss(j, "userID", actor);
    }
    if (strcmp(method, "registerUser") == 0 || strcmp(method, "updateUserInfo") == 0
        || strcmp(method, "updateUserInfoEx") == 0 || strcmp(method, "setGlobalRecvMessageOpt") == 0) {
        if (require_fields(j, resp, "userID", (const char *)NULL)) { miku_json_destroy(j); return; }
    } else if (strcmp(method, "getAllUsers") == 0 || strcmp(method, "getAllUsersUID") == 0
               || strcmp(method, "getUserCount") == 0
               || strcmp(method, "getUsersOnlineTokenDetail") == 0
               || strcmp(method, "addNotificationAccount") == 0
               || strcmp(method, "updateNotificationAccount") == 0
               || strcmp(method, "searchNotificationAccount") == 0) {
        if (plat != 5) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "searchUser") == 0) {
        if (require_fields(j, resp, "keyword", (const char *)NULL)) { miku_json_destroy(j); return; }
    }
    if (plat != 5 && actor[0]) {
        if (strcmp(method, "getUsersOnlineStatus") == 0) {
            filter_presence_user_id_list(c, plat, actor, j, "userIDList");
        } else if (strcmp(method, "getUserStatus") == 0 || strcmp(method, "getSubscribeUsersStatus") == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            if (uid && uid[0] && !api_may_view_user(c, plat, actor, uid)) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_ji(out, "errCode", 0);
                miku_json_object_set(out, "data", miku_json_create_null());
                json_resp(resp, out);
                return;
            }
        } else if (strcmp(method, "subscribeOrCancelUserStatus") == 0) {
            filter_presence_user_id_list(c, plat, actor, j, "userIDList");
            filter_presence_user_id_list(c, plat, actor, j, "subscribeUserIDList");
        }
    }
    miku_user_handle_rpc(c->user, method, j, out);
    /* Non-admin search: exact userID only — block nickname/userID substring sweeps. */
    if (strcmp(method, "searchUser") == 0 && plat != 5) {
        const char *kw = miku_json_str(miku_json_get(j, "keyword"));
        filter_users_read_result(c, plat, actor, kw, out);
    }
    if (plat != 5 && actor[0]) {
        if (strcmp(method, "getUserInfo") == 0) {
            miku_json_val_t *data = miku_json_get(out, "data");
            const char *uid = data ? miku_json_str(miku_json_get(data, "userID")) : NULL;
            if (!uid || !api_may_view_user(c, plat, actor, uid)) {
                miku_ji(out, "errCode", 0);
                miku_json_object_set(out, "data", miku_json_create_null());
            }
        } else if (strcmp(method, "getUsersInfo") == 0) {
            filter_users_read_result(c, plat, actor, NULL, out);
        }
    }
    if (c->webhook && strcmp(method, "registerUser") == 0) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            char payload[256];
            snprintf(payload, sizeof(payload), "{\"event\":\"userRegistered\",\"userID\":\"%s\"}", uid ? uid : "");
            miku_webhook_fire(c->webhook, MK_WH_USER_ONLINE, payload);
        }
    }
        miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_friend(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "getFriendList");
    char actor[128] = {0};
    int plat = req_token_platform(req);
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        if (strcmp(method, "addFriend") == 0 || strcmp(method, "addBlack") == 0
            || strcmp(method, "deleteFriend") == 0 || strcmp(method, "removeBlack") == 0
            || strcmp(method, "setFriendRemark") == 0 || strcmp(method, "getFriendList") == 0
            || strcmp(method, "getBlackList") == 0
            || strcmp(method, "importFriend") == 0
            || strcmp(method, "respondFriendApply") == 0
            || strcmp(method, "updateFriends") == 0
            || strcmp(method, "acceptFriendApply") == 0
            || strcmp(method, "refuseFriendApply") == 0
            || strcmp(method, "getSpecifiedFriendsInfo") == 0
            || strcmp(method, "getFriendIDs") == 0
            || strcmp(method, "getFullFriendUserIDs") == 0
            || strcmp(method, "syncFriend") == 0
            || strcmp(method, "getDesignatedFriends") == 0
            || strcmp(method, "getFriendApplyList") == 0
            || strcmp(method, "getSelfApplyList") == 0
            || strcmp(method, "getDesignatedFriendApply") == 0
            || strcmp(method, "getSpecifiedBlacks") == 0
            || strcmp(method, "getSelfUnhandledApplyCount") == 0) {
            miku_jss(j, "ownerUserID", actor);
            miku_jss(j, "userID", actor);
        }
        if (strcmp(method, "isFriend") == 0)
            miku_jss(j, "userID", actor);
    }
    if (strcmp(method, "addFriend") == 0 || strcmp(method, "addBlack") == 0
        || strcmp(method, "deleteFriend") == 0 || strcmp(method, "removeBlack") == 0) {
        if (require_fields(j, resp, "ownerUserID", "friendUserID", (const char *)NULL)) { miku_json_destroy(j); return; }
    } else if (strcmp(method, "setFriendRemark") == 0) {
        if (require_fields(j, resp, "ownerUserID", "friendUserID", "remark", (const char *)NULL)) { miku_json_destroy(j); return; }
    } else if (strcmp(method, "isFriend") == 0) {
        if (require_fields(j, resp, "userID", "friendUserID", (const char *)NULL)) { miku_json_destroy(j); return; }
    } else if (strcmp(method, "importFriend") == 0) {
        if (plat != 5) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    }
    miku_friend_handle_rpc(c->friend_svc, method, j, out);
    if (strcmp(method, "isFriend") == 0 && plat != 5 && actor[0]) {
        const char *fuid = miku_json_str(miku_json_get(j, "friendUserID"));
        if (!fuid) fuid = miku_json_str(miku_json_get(j, "userID2"));
        int mutual = fuid && miku_friend_is_mutual(c->friend_svc, actor, fuid);
        miku_ji(out, "isFriend", mutual ? 1 : 0);
    }
    if (c->on_blacklist &&
        (strcmp(method, "addBlack") == 0 || strcmp(method, "removeBlack") == 0)) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
            const char *uid = miku_json_str(miku_json_get(j, "friendUserID"));
            if (!uid) uid = miku_json_str(miku_json_get(j, "userID"));
            if (owner && owner[0] && uid && uid[0])
                c->on_blacklist(owner, uid, strcmp(method, "removeBlack") == 0,
                                c->on_blacklist_ctx);
        }
    }
    if (c->webhook && strcmp(method, "addFriend") == 0) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
            const char *fuid = miku_json_str(miku_json_get(j, "friendUserID"));
            char payload[512];
            snprintf(payload, sizeof(payload), "{\"event\":\"friendAdded\",\"ownerUserID\":\"%s\",\"friendUserID\":\"%s\"}",
                     owner ? owner : "", fuid ? fuid : "");
            miku_webhook_fire(c->webhook, MK_WH_AFTER_ADD_FRIEND, payload);
        }
    }
        miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_group(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "getGroupInfo");
    char actor[128] = {0};
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        if (strcmp(method, "createGroup") == 0)
            miku_jss(j, "ownerUserID", actor);
        else if (strcmp(method, "joinGroup") == 0 || strcmp(method, "quitGroup") == 0
                 || strcmp(method, "dismissGroup") == 0
                 || strcmp(method, "transferGroupOwner") == 0)
            miku_jss(j, "userID", actor);
        else if (strcmp(method, "kickGroupMember") == 0
                 || strcmp(method, "setGroupInfo") == 0
                 || strcmp(method, "setGroupInfoEx") == 0
                 || strcmp(method, "muteGroup") == 0
                 || strcmp(method, "cancelMuteGroup") == 0
                 || strcmp(method, "muteGroupMember") == 0
                 || strcmp(method, "cancelMuteGroupMember") == 0
                 || strcmp(method, "setGroupMemberInfo") == 0)
            miku_jss(j, "opUserID", actor);
        else if (strcmp(method, "inviteToGroup") == 0)
            miku_jss(j, "fromUserID", actor);
        else if (strcmp(method, "getJoinedGroupList") == 0
                 || strcmp(method, "getFullJoinGroupIDs") == 0) {
            miku_jss(j, "userID", actor);
            miku_jss(j, "ownerUserID", actor);
        }
    }
    if (strcmp(method, "createGroup") == 0) {
        if (require_fields(j, resp, "ownerUserID", "groupName", (const char *)NULL)) { miku_json_destroy(j); return; }
        const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
        if (owner && owner[0]) filter_group_invitee_ids(c, owner, j);
    } else if (strcmp(method, "joinGroup") == 0 || strcmp(method, "quitGroup") == 0
               || strcmp(method, "dismissGroup") == 0
               || strcmp(method, "transferGroupOwner") == 0) {
        if (require_fields(j, resp, "userID", "groupID", (const char *)NULL)) { miku_json_destroy(j); return; }
    } else if (strcmp(method, "inviteToGroup") == 0 || strcmp(method, "kickGroupMember") == 0) {
        if (require_fields(j, resp, "groupID", (const char *)NULL)) { miku_json_destroy(j); return; }
        if (!miku_json_get(j, "userID") && !miku_json_get(j, "invitedUserIDs") &&
            !miku_json_get(j, "kickedUserIDs")) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":400,\"errMsg\":\"missing userID\"}");
            return;
        }
        if (strcmp(method, "inviteToGroup") == 0 && actor[0])
            filter_group_invitee_ids(c, actor, j);
        if (strcmp(method, "kickGroupMember") == 0 &&
            !miku_json_get(j, "opUserID") && !miku_json_get(j, "fromUserID") &&
            !miku_json_get(j, "ownerUserID")) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":400,\"errMsg\":\"missing opUserID\"}");
            return;
        }
    } else if (strcmp(method, "getGroupMemberList") == 0
               || strcmp(method, "getGroupMemberUserID") == 0
               || strcmp(method, "getFullGroupMemberUserIDs") == 0
               || strcmp(method, "getGroupInfo") == 0
               || strcmp(method, "getGroupAbstractInfo") == 0
               || strcmp(method, "getIncrementalGroupMembers") == 0
               || strcmp(method, "getIncrementalGroupMemberBatch") == 0
               || strcmp(method, "getGroupApplicationList") == 0
               || strcmp(method, "getRecvGroupApplicationList") == 0
               || strcmp(method, "getGroupApplicationUnhandledCount") == 0) {
        const char *gid = miku_json_str(miku_json_get(j, "groupID"));
        if (!actor[0] || !gid || !gid[0]
            || !miku_group_is_member(c->group_svc, gid, actor)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"not a group member\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "getIncrementalJoinGroups") == 0 && actor[0]) {
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        if (!uid || strcmp(uid, actor) != 0) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"userID mismatch\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "getUserReqGroupApplicationList") == 0 && actor[0]) {
        const char *uid = miku_json_str(miku_json_get(j, "userID"));
        if (!uid || strcmp(uid, actor) != 0) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"userID mismatch\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "getGroupsInfo") == 0 && actor[0]) {
        filter_group_id_list(c, actor, j);
    }

    /* Snapshot members before dismiss clears them (for gateway sync). */
    miku_group_member_t dismiss_members[256];
    int dismiss_n = 0;
    const char *dismiss_gid = NULL;
    if (strcmp(method, "dismissGroup") == 0) {
        dismiss_gid = miku_json_str(miku_json_get(j, "groupID"));
        if (dismiss_gid)
            dismiss_n = miku_group_get_members(c->group_svc, dismiss_gid,
                                               dismiss_members, 256);
    }

    miku_group_handle_rpc(c->group_svc, method, j, out);

    /* Sync members to msggateway so split-deploy group PUSH works. */
    if (c->on_group_member) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0 && strcmp(method, "createGroup") == 0) {
            const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
            const char *gid = miku_json_str(miku_json_get(out, "data"));
            if (owner && gid) c->on_group_member(gid, owner, 100, 0, c->on_group_member_ctx);
            miku_json_val_t *ids = miku_json_get(j, "memberUserIDs");
            if (!ids) ids = miku_json_get(j, "invitedUserIDs");
            if (gid && ids && miku_json_type(ids) == MK_JSON_ARRAY) {
                size_t n = miku_json_size(ids);
                for (size_t i = 0; i < n; i++) {
                    const char *u = miku_json_str(miku_json_at(ids, i));
                    if (u && (!owner || strcmp(u, owner) != 0))
                        c->on_group_member(gid, u, 20, 0, c->on_group_member_ctx);
                }
            }
        } else if (err == 0 && strcmp(method, "joinGroup") == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            if (uid && gid) c->on_group_member(gid, uid, 20, 0, c->on_group_member_ctx);
        } else if (err == 0 && strcmp(method, "inviteToGroup") == 0) {
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            if (gid && uid) c->on_group_member(gid, uid, 20, 0, c->on_group_member_ctx);
            miku_json_val_t *ids = miku_json_get(j, "invitedUserIDs");
            if (gid && ids && miku_json_type(ids) == MK_JSON_ARRAY) {
                size_t n = miku_json_size(ids);
                for (size_t i = 0; i < n; i++) {
                    const char *u = miku_json_str(miku_json_at(ids, i));
                    if (u) c->on_group_member(gid, u, 20, 0, c->on_group_member_ctx);
                }
            }
        } else if (err == 0 && strcmp(method, "quitGroup") == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            if (uid && gid) {
                c->on_group_member(gid, uid, 0, 1, c->on_group_member_ctx);
                api_drop_group_conv(c->conv, uid, gid);
            }
        } else if (err == 0 && strcmp(method, "kickGroupMember") == 0) {
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            if (gid && uid) {
                c->on_group_member(gid, uid, 0, 1, c->on_group_member_ctx);
                api_drop_group_conv(c->conv, uid, gid);
            }
            miku_json_val_t *ids = miku_json_get(j, "kickedUserIDs");
            if (!ids) ids = miku_json_get(j, "invitedUserIDs");
            if (gid && ids && miku_json_type(ids) == MK_JSON_ARRAY) {
                size_t n = miku_json_size(ids);
                for (size_t i = 0; i < n; i++) {
                    const char *u = miku_json_str(miku_json_at(ids, i));
                    if (u) {
                        c->on_group_member(gid, u, 0, 1, c->on_group_member_ctx);
                        api_drop_group_conv(c->conv, u, gid);
                    }
                }
            }
        } else if (err == 0 && strcmp(method, "dismissGroup") == 0 && dismiss_gid) {
            for (int i = 0; i < dismiss_n; i++) {
                c->on_group_member(dismiss_gid, dismiss_members[i].user_id, 0, 1,
                                   c->on_group_member_ctx);
                api_drop_group_conv(c->conv, dismiss_members[i].user_id, dismiss_gid);
            }
        }
    }

    if (c->webhook) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0 && strcmp(method, "createGroup") == 0) {
            const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
            const char *gid = miku_json_str(miku_json_get(out, "data"));
            char payload[512];
            snprintf(payload, sizeof(payload), "{\"event\":\"groupCreated\",\"ownerUserID\":\"%s\",\"groupID\":\"%s\"}",
                     owner ? owner : "", gid ? gid : "");
            miku_webhook_fire(c->webhook, MK_WH_AFTER_CREATE_GROUP, payload);
        } else if (err == 0 && strcmp(method, "joinGroup") == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            char payload[512];
            snprintf(payload, sizeof(payload), "{\"event\":\"groupJoined\",\"userID\":\"%s\",\"groupID\":\"%s\"}",
                     uid ? uid : "", gid ? gid : "");
            miku_webhook_fire(c->webhook, MK_WH_AFTER_JOIN_GROUP, payload);
        }
    }
    if (actor[0] && strcmp(method, "getGroupsInfo") == 0)
        filter_groups_read_result(c, actor, out);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_conv(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "getConversation");
    char actor[128] = {0};
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        miku_jss(j, "ownerUserID", actor);
        miku_jss(j, "userID", actor);
    }
    if (strcmp(method, "setConversation") == 0 || strcmp(method, "deleteConversation") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) { miku_json_destroy(j); return; }
        const char *owner = miku_json_str(miku_json_get(j, "ownerUserID"));
        if (!owner || !owner[0]) owner = miku_json_str(miku_json_get(j, "userID"));
        if (!owner || !owner[0]) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":400,\"errMsg\":\"missing required fields: userID or ownerUserID\"}");
            resp->status = 400;
            return;
        }
    }
    if (conv_mutates(method) && api_conv_ids_denied(c, actor, j)) {
        miku_json_destroy(j); miku_json_destroy(out);
        miku_http_response_set_json(resp,
            "{\"errCode\":3003,\"errMsg\":\"not a conversation participant\"}");
        resp->status = 403;
        return;
    }
    miku_conv_handle_rpc(c->conv, method, j, out);
    if (conv_is_read(method) && actor[0]) {
        filter_conv_read_result(c, actor, out);
        if (strcmp(method, "getTotalUnreadMsgCount") == 0)
            filter_conv_unread_count(c, actor, out);
    }
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_msg(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "sendMsg");
    char actor[128] = {0};
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        if (strcmp(method, "sendMsg") == 0 || strcmp(method, "sendSimpleMsg") == 0
            || strcmp(method, "send") == 0
            || strcmp(method, "sendBusinessNotification") == 0)
            miku_jss(j, "sendID", actor);
        else if (strcmp(method, "deleteMsg") == 0 || strcmp(method, "revokeMsg") == 0
                 || strcmp(method, "deleteMsgPhysical") == 0
                 || strcmp(method, "deleteMsgPhysicalBySeq") == 0
                 || strcmp(method, "userClearAllMsg") == 0
                 || strcmp(method, "clearConversationMsg") == 0
                 || strcmp(method, "markConversationAsRead") == 0
                 || strcmp(method, "setConversationHasReadSeq") == 0
                 || strcmp(method, "markMsgsAsRead") == 0
                 || strcmp(method, "markMsgAsRead") == 0
                 || strcmp(method, "getMsg") == 0
                 || strcmp(method, "checkMsgIsSendSuccess") == 0
                 || strcmp(method, "getSendMsgStatus") == 0
                 || strcmp(method, "getMsgByConv") == 0
                 || strcmp(method, "pullMsgBySeq") == 0
                 || strcmp(method, "getMsgBySeq") == 0
                 || strcmp(method, "searchMsg") == 0
                 || strcmp(method, "getNewestSeq") == 0
                 || strcmp(method, "getConversationsHasReadAndMaxSeq") == 0)
            miku_jss(j, "userID", actor);
        else if (strcmp(method, "setMessageReactionExtensions") == 0
                 || strcmp(method, "getMessageListReactionExtensions") == 0
                 || strcmp(method, "addMessageReactionExtensions") == 0
                 || strcmp(method, "deleteMessageReactionExtensions") == 0)
            miku_jss(j, "userID", actor);
    }
    if (strcmp(method, "sendBusinessNotification") == 0) {
        if (req_token_platform(req) != 5) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    }
    if (strcmp(method, "sendMsg") == 0 || strcmp(method, "sendSimpleMsg") == 0
        || strcmp(method, "send") == 0
        || strcmp(method, "sendBusinessNotification") == 0) {
        if (strcmp(method, "sendBusinessNotification") != 0) {
            if (require_fields(j, resp, "sendID", "content", (const char *)NULL)) {
                miku_json_destroy(j); return;
            }
        } else if (require_fields(j, resp, "sendID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *rid = miku_json_str(miku_json_get(j, "recvID"));
        const char *gid = miku_json_str(miku_json_get(j, "groupID"));
        if ((!rid || !rid[0]) && (!gid || !gid[0])) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":400,\"errMsg\":\"missing required fields: recvID or groupID\"}");
            resp->status = 400;
            return;
        }
        /* Single chat: mutual friends required; blacklist blocks either direction. */
        if (rid && rid[0] && (!gid || !gid[0])) {
            const char *sid = miku_json_str(miku_json_get(j, "sendID"));
            if (!sid || !sid[0] || !c->friend_svc) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":6002,\"errMsg\":\"not mutual friends\"}");
                resp->status = 403;
                return;
            }
            if (!miku_friend_is_mutual(c->friend_svc, sid, rid)) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":6002,\"errMsg\":\"not mutual friends\"}");
                resp->status = 403;
                return;
            }
            if (miku_friend_is_black(c->friend_svc, sid, rid) ||
                miku_friend_is_black(c->friend_svc, rid, sid)) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":6001,\"errMsg\":\"blocked by blacklist\"}");
                resp->status = 403;
                return;
            }
        }
        /* Group chat: sender must be a member. */
        if (gid && gid[0]) {
            const char *sid = miku_json_str(miku_json_get(j, "sendID"));
            if (!sid || !sid[0] || !c->group_svc ||
                !miku_group_is_member(c->group_svc, gid, sid)) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":3003,\"errMsg\":\"not a group member\"}");
                resp->status = 403;
                return;
            }
        }
    } else if (strcmp(method, "deleteMsg") == 0 || strcmp(method, "revokeMsg") == 0) {
        if (require_fields(j, resp, "userID", "clientMsgID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cmid = miku_json_str(miku_json_get(j, "clientMsgID"));
        if (!actor[0] || !cmid || !cmid[0] ||
            !miku_msg_may_delete_physical(c->msg, actor, cmid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":5001,\"errMsg\":\"forbidden\"}");
            return;
        }
    } else if (strcmp(method, "searchMsg") == 0) {
        if (require_fields(j, resp, "keyword", (const char *)NULL)) { miku_json_destroy(j); return; }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (req_token_platform(req) != 5 && (!cid || !cid[0])) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"data\":[]}");
            return;
        }
        if (cid && cid[0] && (!actor[0] || !api_may_access_conv(c, actor, cid))) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"data\":[]}");
            return;
        }
    } else if (strcmp(method, "getMsgByConv") == 0 || strcmp(method, "pullMsgBySeq") == 0
               || strcmp(method, "getMsgBySeq") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"data\":[]}");
            return;
        }
        if (strcmp(method, "getMsgBySeq") == 0) {
            int64_t seq = miku_json_int(miku_json_get(j, "seq"));
            if (seq <= 0) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":400,\"errMsg\":\"missing required fields: seq\"}");
                resp->status = 400;
                return;
            }
        }
    } else if (strcmp(method, "getNewestSeq") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"seq\":0}");
            return;
        }
    } else if (strcmp(method, "markConversationAsRead") == 0
               || strcmp(method, "setConversationHasReadSeq") == 0
               || strcmp(method, "markMsgsAsRead") == 0
               || strcmp(method, "markMsgAsRead") == 0
               || strcmp(method, "clearConversationMsg") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"not a conversation participant\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "userClearAllMsg") == 0) {
        if (require_fields(j, resp, "userID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        if (!actor[0]) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"unauthorized\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "getMsg") == 0) {
        if (require_fields(j, resp, "serverMsgID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        if (!actor[0]) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"data\":[]}");
            return;
        }
    } else if (strcmp(method, "getSendMsgStatus") == 0
               || strcmp(method, "checkMsgIsSendSuccess") == 0) {
        const char *smid = miku_json_str(miku_json_get(j, "serverMsgID"));
        const char *cmid = miku_json_str(miku_json_get(j, "clientMsgID"));
        if ((!smid || !smid[0]) && (!cmid || !cmid[0])) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":400,\"errMsg\":\"missing required fields: serverMsgID or clientMsgID\"}");
            resp->status = 400;
            return;
        }
        if (!actor[0]) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"unauthorized\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "setMessageReactionExtensions") == 0
               || strcmp(method, "addMessageReactionExtensions") == 0
               || strcmp(method, "deleteMessageReactionExtensions") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !cid || !cid[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":3003,\"errMsg\":\"not a conversation participant\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "getMessageListReactionExtensions") == 0) {
        if (require_fields(j, resp, "conversationID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":0,\"data\":[]}");
            return;
        }
    } else if (strcmp(method, "cleanUpMsg") == 0) {
        if (req_token_platform(req) != 5) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "batchSendMsg") == 0) {
        if (req_token_platform(req) != 5) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    } else if (strcmp(method, "deleteMsgPhysical") == 0) {
        if (require_fields(j, resp, "userID", "clientMsgID", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cmid = miku_json_str(miku_json_get(j, "clientMsgID"));
        if (!actor[0] || !cmid || !cmid[0] ||
            !miku_msg_may_delete_physical(c->msg, actor, cmid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":5001,\"errMsg\":\"forbidden\"}");
            return;
        }
    } else if (strcmp(method, "deleteMsgPhysicalBySeq") == 0) {
        if (require_fields(j, resp, "userID", "conversationID", "seq", (const char *)NULL)) {
            miku_json_destroy(j); return;
        }
        const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
        if (!actor[0] || !cid || !cid[0] || !api_may_access_conv(c, actor, cid)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":5001,\"errMsg\":\"forbidden\"}");
            return;
        }
        int64_t del_seq = miku_json_int(miku_json_get(j, "seq"));
        if (!miku_msg_may_delete_physical_by_seq(c->msg, actor, cid, del_seq)) {
            miku_json_destroy(j); miku_json_destroy(out);
            miku_http_response_set_json(resp, "{\"errCode\":5001,\"errMsg\":\"forbidden\"}");
            return;
        }
    }
    if (strcmp(method, "cleanUpMsg") == 0 || strcmp(method, "batchSendMsg") == 0
        || strcmp(method, "sendBusinessNotification") == 0)
        miku_ji(j, "platformID", req_token_platform(req));
    miku_msg_handle_rpc(c->msg, method, j, out);

    /* getMsg / getMsgBySeq / searchMsg: drop non-participant results (no 3003 oracle). */
    if (actor[0] && (strcmp(method, "getMsg") == 0 || strcmp(method, "getMsgBySeq") == 0
                     || strcmp(method, "searchMsg") == 0 || strcmp(method, "getMsgByConv") == 0))
        filter_msg_read_result(c, actor, out);
    if (actor[0] && strcmp(method, "getConversationsHasReadAndMaxSeq") == 0)
        filter_conv_read_result(c, actor, out);

    if (strcmp(method, "sendMsg") == 0 || strcmp(method, "sendSimpleMsg") == 0
        || strcmp(method, "send") == 0) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *send_id = miku_json_str(miku_json_get(j, "sendID"));
            const char *recv_id = miku_json_str(miku_json_get(j, "recvID"));
            const char *group_id = miku_json_str(miku_json_get(j, "groupID"));
            const char *content = miku_json_str(miku_json_get(j, "content"));
            int64_t send_time = miku_json_int(miku_json_get(out, "sendTime"));
            char cid[MK_CONV_ID_LEN];
            if (send_id && group_id && group_id[0]) {
                miku_conversation_id_resolve(cid, sizeof(cid), NULL, group_id, send_id, NULL);
                group_conv_upsert_ctx_t gctx = {
                    .svc = c->conv, .cid = cid, .gid = group_id,
                    .send_id = send_id, .send_time = send_time, .content = content,
                };
                miku_group_foreach_member(c->group_svc, group_id, upsert_group_member_conv, &gctx);
                /* Ensure sender has a row even if not yet in group_svc (split race). */
                upsert_conv_on_send(c->conv, send_id, cid, MK_IM_CONV_GROUP,
                                   NULL, group_id, send_time, content, 0);
            } else if (send_id && recv_id && recv_id[0]) {
                miku_conversation_id_resolve(cid, sizeof(cid), NULL, NULL, send_id, recv_id);
                upsert_conv_on_send(c->conv, send_id, cid, MK_IM_CONV_SINGLE,
                                   recv_id, NULL, send_time, content, 0);
                upsert_conv_on_send(c->conv, recv_id, cid, MK_IM_CONV_SINGLE,
                                   send_id, NULL, send_time, content, 1);
            }
            if (c->on_msg_sent) {
                miku_im_msg_t im;
                miku_im_msg_from_json(&im, j);
                if (!im.conversation_type) {
                    if (im.group_id[0]) im.conversation_type = MK_IM_CONV_GROUP;
                    else if (im.recv_id[0]) im.conversation_type = MK_IM_CONV_SINGLE;
                }
                if (im.content_type <= 0) {
                    int64_t mt = miku_json_int(miku_json_get(j, "msgType"));
                    im.content_type = mt > 0 ? (int)mt : MK_IM_MSG_TYPE_TEXT;
                }
                const char *smid = miku_json_str(miku_json_get(out, "serverMsgID"));
                if (smid && smid[0])
                    strncpy(im.msg_id, smid, sizeof(im.msg_id) - 1);
                if (send_time > 0) im.send_time = send_time;
                if (c->on_msg_sent(&im, c->on_msg_sent_ctx) == 0) {
                    if (im.seq > 0) miku_ji(out, "seq", im.seq);
                    if (im.send_time > 0) miku_ji(out, "sendTime", im.send_time);
                    if (im.msg_id[0]) miku_jss(out, "serverMsgID", im.msg_id);
                    if (im.client_msg_id[0] && (im.seq > 0 || im.msg_id[0]))
                        miku_msg_update_delivery(c->msg, im.client_msg_id, im.seq,
                                                 im.msg_id, im.send_time);
                }
            }
        }
    }

    if (strcmp(method, "markConversationAsRead") == 0 ||
        strcmp(method, "setConversationHasReadSeq") == 0 ||
        strcmp(method, "markMsgsAsRead") == 0) {
        int64_t mark_err = miku_json_int(miku_json_get(out, "errCode"));
        if (mark_err == 0) {
            const char *owner = miku_json_str(miku_json_get(j, "userID"));
            if (!owner || !owner[0]) owner = miku_json_str(miku_json_get(j, "ownerUserID"));
            const char *cid = miku_json_str(miku_json_get(j, "conversationID"));
            if (owner && owner[0] && cid && cid[0]) {
                miku_conversation_t cv;
                if (miku_conv_get(c->conv, owner, cid, &cv) == 0) {
                    cv.unread_count = 0;
                    miku_conv_update(c->conv, &cv);
                }
            }
        }
    }

    if (c->webhook) {
        int64_t wh_err = miku_json_int(miku_json_get(out, "errCode"));
        if (wh_err == 0 &&
            (strcmp(method, "sendMsg") == 0 || strcmp(method, "sendSimpleMsg") == 0
             || strcmp(method, "send") == 0)) {
            const char *sid = miku_json_str(miku_json_get(j, "sendID"));
            const char *rid = miku_json_str(miku_json_get(j, "recvID"));
            const char *gid = miku_json_str(miku_json_get(j, "groupID"));
            const char *smid = miku_json_str(miku_json_get(out, "serverMsgID"));
            char payload[1024];
            snprintf(payload, sizeof(payload),
                     "{\"event\":\"msgSent\",\"sendID\":\"%s\",\"recvID\":\"%s\","
                     "\"groupID\":\"%s\",\"serverMsgID\":\"%s\"}",
                     sid ? sid : "", rid ? rid : "", gid ? gid : "", smid ? smid : "");
            miku_webhook_fire(c->webhook, MK_WH_AFTER_SEND_MSG, payload);
        } else if (wh_err == 0 && strcmp(method, "revokeMsg") == 0) {
            const char *cmid = miku_json_str(miku_json_get(j, "clientMsgID"));
            char payload[512];
            snprintf(payload, sizeof(payload), "{\"event\":\"msgRevoked\",\"clientMsgID\":\"%s\"}", cmid ? cmid : "");
            miku_webhook_fire(c->webhook, MK_WH_MSG_REVOKE, payload);
        }
    }

        miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_third(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    const char *method = api_rpc_method(req, "getUploadToken");
    if (strcmp(method, "getPrometheus") == 0 && req_token_platform(req) != 5) {
        miku_json_destroy(j); miku_json_destroy(out);
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    if ((strcmp(method, "searchLogs") == 0 || strcmp(method, "deleteLogs") == 0
         || strcmp(method, "uploadLogs") == 0)
        && req_token_platform(req) != 5) {
        miku_json_destroy(j); miku_json_destroy(out);
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    char actor[128] = {0};
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        if (strcmp(method, "getPrometheus") != 0)
            miku_jss(j, "userID", actor);
        /* Object ACL: path fields must be actor/... when present. */
        static const char *path_keys[] = {"name", "key", "filePath", "objectName", NULL};
        const char *obj_path = NULL;
        for (int i = 0; path_keys[i] && !obj_path; i++)
            obj_path = miku_json_str(miku_json_get(j, path_keys[i]));
        static const char *acl_methods[] = {
            "deleteObject", "accessURL", "getDownloadURL", "getObjectInfo",
            "initiateMultipartUpload", "completeMultipartUpload",
            "authSign", "completeFormData", "initiateFormData", NULL
        };
        int needs_acl = 0;
        for (int i = 0; acl_methods[i]; i++) {
            if (strcmp(method, acl_methods[i]) == 0) { needs_acl = 1; break; }
        }
        if (needs_acl) {
            if (!obj_path || !obj_path[0]) {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":3003,\"errMsg\":\"object path required\"}");
                resp->status = 403;
                return;
            }
            size_t alen = strlen(actor);
            if (strncmp(obj_path, actor, alen) != 0 || obj_path[alen] != '/') {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":3003,\"errMsg\":\"object access denied\"}");
                resp->status = 403;
                return;
            }
        } else if (obj_path && obj_path[0]) {
            size_t alen = strlen(actor);
            if (strncmp(obj_path, actor, alen) != 0 || obj_path[alen] != '/') {
                miku_json_destroy(j); miku_json_destroy(out);
                miku_http_response_set_json(resp,
                    "{\"errCode\":3003,\"errMsg\":\"object access denied\"}");
                resp->status = 403;
                return;
            }
        }
    }
    miku_third_handle_rpc(c->third, method, j, out);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_admin(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    char path[128];
    api_req_path(req, path, sizeof(path));
    /* health is public; stats/shutdown need admin token (platform 5). */
    if (strcmp(path, "/admin/health") != 0) {
        if (verify_token(c, req, resp)) return;
        if (req_token_platform(req) != 5) {
            miku_http_response_set_json(resp,
                "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
            resp->status = 403;
            return;
        }
    }
    miku_json_val_t *out = miku_json_create_object();
    if (strcmp(path, "/admin/stats") == 0) {
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
    } else if (strcmp(path, "/admin/health") == 0) {
        miku_ji(out, "status", 0);
        miku_jss(out, "message", "ok");
    } else if (strcmp(path, "/admin/shutdown") == 0) {
        miku_ji(out, "errCode", 0);
        miku_jss(out, "message", "shutdown scheduled");
    } else {
        miku_ji(out, "errCode", 404);
    }
    json_resp(resp, out);
}

static void handle_metrics(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (verify_token(c, req, resp)) return;
    if (req_token_platform(req) != 5) {
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    (void)req;
    miku_stats_snapshot_t snap;
    miku_stats_snapshot(&c->stats, &snap);

    char buf[2048];
    int n = snprintf(buf, sizeof(buf),
        "# HELP miku_requests_total Total requests processed\n"
        "# TYPE miku_requests_total counter\n"
        "miku_requests_total %lld\n"
        "# HELP miku_requests_failed Total failed requests\n"
        "# TYPE miku_requests_failed counter\n"
        "miku_requests_failed %lld\n"
        "# HELP miku_connections_active Currently active connections\n"
        "# TYPE miku_connections_active gauge\n"
        "miku_connections_active %lld\n"
        "# HELP miku_connections_total Total connections opened\n"
        "# TYPE miku_connections_total counter\n"
        "miku_connections_total %lld\n"
        "# HELP miku_bytes_sent Total bytes sent\n"
        "# TYPE miku_bytes_sent counter\n"
        "miku_bytes_sent %lld\n"
        "# HELP miku_bytes_recv Total bytes received\n"
        "# TYPE miku_bytes_recv counter\n"
        "miku_bytes_recv %lld\n"
        "# HELP miku_uptime_ms Service uptime in milliseconds\n"
        "# TYPE miku_uptime_ms gauge\n"
        "miku_uptime_ms %lld\n",
        (long long)snap.requests_total,
        (long long)snap.requests_failed,
        (long long)snap.connections_active,
        (long long)snap.connections_total,
        (long long)snap.bytes_sent,
        (long long)snap.bytes_recv,
        (long long)snap.uptime_ms);

    resp->status = 200;
    if (!resp->headers) resp->headers = miku_hashmap_create(4, free);
    miku_hashmap_put(resp->headers, "Content-Type", strdup("text/plain; version=0.0.4"));
    if (resp->body) { miku_str_destroy(resp->body); resp->body = NULL; }
    miku_string_t *body = miku_str_create_empty((size_t)n + 1);
    miku_str_cat_len(body, buf, (size_t)n);
    resp->body = body;
}

static void handle_batch(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char path[128];
    api_req_path(req, path, sizeof(path));
    if (strcmp(path, "/batch/get_users_info") == 0) {
        char actor[128] = {0};
        int plat = req_token_platform(req);
        req_token_uid(c, req, actor, sizeof(actor));
        miku_json_val_t *uid_list = miku_json_get(j, "userIDList");
        miku_json_val_t *arr = miku_json_create_array();
        if (uid_list) {
            size_t n = miku_json_size(uid_list);
            for (size_t i = 0; i < n; i++) {
                const char *uid = miku_json_str(miku_json_at(uid_list, i));
                if (!api_may_view_user(c, plat, actor, uid)) continue;
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
    } else if (strcmp(path, "/batch/delete_friend") == 0) {
        char actor[128] = {0};
        if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0])
            miku_jss(j, "ownerUserID", actor);
        miku_json_val_t *r = miku_json_create_object();
        miku_friend_handle_rpc(c->friend_svc, "deleteFriend", j, r);
        miku_json_object_set(out, "errCode", miku_json_get(r, "errCode"));
        miku_json_destroy(r);
    } else {
        miku_ji(out, "errCode", 404);
    }
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

static void handle_statistics(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (verify_token(c, req, resp)) return;
    if (req_token_platform(req) != 5) {
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char path[128];
    api_req_path(req, path, sizeof(path));
    const char *method = "userRegisterCount";
    if (strcmp(path, "/statistics/user/active") == 0) method = "getActiveUser";
    else if (strcmp(path, "/statistics/user/register") == 0) method = "userRegisterCount";
    else if (strcmp(path, "/statistics/group/active") == 0) method = "getActiveGroup";
    else if (strcmp(path, "/statistics/group/create") == 0) method = "groupCreateCount";
    miku_ji(out, "errCode", 0);
    miku_jss(out, "method", method);
    miku_ji(out, "count", 0);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_jssdk(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (check_ratelimit(c, req, resp)) return;
    if (verify_token(c, req, resp)) return;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char path[128];
    api_req_path(req, path, sizeof(path));
    const char *method = (strcmp(path, "/jssdk/get_active_conversations") == 0)
        ? "getActiveConversations" : "getConversations";
    char actor[128] = {0};
    if (req_token_uid(c, req, actor, sizeof(actor)) == 0 && actor[0]) {
        miku_jss(j, "ownerUserID", actor);
        miku_jss(j, "userID", actor);
    }
    miku_conv_handle_rpc(c->conv, method, j, out);
    if (actor[0]) filter_conv_read_result(c, actor, out);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_prometheus_discovery(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (verify_token(c, req, resp)) return;
    if (req_token_platform(req) != 5) {
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    (void)req;
    miku_json_val_t *out = miku_json_create_object();
    miku_json_val_t *targets = miku_json_create_array();
    miku_json_val_t *item = miku_json_create_object();
    miku_jss(item, "targets", "127.0.0.1:10002");
    miku_json_array_push(targets, item);
    miku_json_object_set(out, "targets", targets);
    json_resp(resp, out);
}

static void handle_config(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (verify_token(c, req, resp)) return;
    if (req_token_platform(req) != 5) {
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char path[128];
    api_req_path(req, path, sizeof(path));
    const char *method = "getConfigList";
    if (strcmp(path, "/config/get_config_list") == 0) method = "getConfigList";
    else if (strcmp(path, "/config/get_config") == 0) method = "getConfig";
    else if (strcmp(path, "/config/set_config") == 0) method = "setConfig";
    else if (strcmp(path, "/config/reset_config") == 0) method = "resetConfig";
    else if (strcmp(path, "/config/set_enable_config_manager") == 0) method = "setEnableConfigManager";
    else if (strcmp(path, "/config/get_enable_config_manager") == 0) method = "getEnableConfigManager";
    miku_ji(out, "errCode", 0);
    miku_jss(out, "method", method);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_restart(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    if (verify_token(c, req, resp)) return;
    if (req_token_platform(req) != 5) {
        miku_http_response_set_json(resp,
            "{\"errCode\":403,\"errMsg\":\"admin token required\"}");
        resp->status = 403;
        return;
    }
    (void)req;
    miku_json_val_t *out = miku_json_create_object();
    miku_ji(out, "errCode", 0);
    miku_jss(out, "message", "restart scheduled");
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

    miku_http_server_route(srv, "POST", "/user/update_user_info_ex",        handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_all_users_uid",          handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_users",                  handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_users_online_token_detail", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command_add",   handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command_delete",handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command_update",handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command_get",   handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/process_user_command_get_all", handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/add_notification_account",   handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/update_notification_account",handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/search_notification_account",handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/get_user_client_config",     handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/set_user_client_config",     handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/del_user_client_config",     handle_user, ctx);
    miku_http_server_route(srv, "POST", "/user/page_user_client_config",    handle_user, ctx);

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

    miku_http_server_route(srv, "POST", "/friend/add_friend_response",       handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/set_friend_remark",         handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_designated_friends",     handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_specified_blacks",       handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_incremental_blacks",     handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_incremental_friends",    handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_friend_id",             handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_specified_friends_info", handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/update_friends",            handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_full_friend_user_ids",  handle_friend, ctx);
    miku_http_server_route(srv, "POST", "/friend/get_self_unhandled_apply_count", handle_friend, ctx);

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

    miku_http_server_route(srv, "POST", "/group/set_group_info_ex",                   handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_recv_group_applicationList",       handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_user_req_group_applicationList",   handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_users_req_application_list", handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_specified_user_group_request_info",handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_abstract_info",              handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_groups",                           handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_incremental_join_groups",          handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_incremental_group_members",        handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_incremental_group_members_batch",  handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_full_group_member_user_ids",       handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_full_join_group_ids",              handle_group, ctx);
    miku_http_server_route(srv, "POST", "/group/get_group_application_unhandled_count",handle_group, ctx);

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

    miku_http_server_route(srv, "POST", "/conversation/get_sorted_conversation_list", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_full_conversation_ids",     handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_incremental_conversations", handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_owner_conversation",        handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_not_notify_conversation_ids",handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/get_pinned_conversation_ids",   handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/delete_conversations",          handle_conv, ctx);
    miku_http_server_route(srv, "POST", "/conversation/update_conversations_by_user",  handle_conv, ctx);

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

    miku_http_server_route(srv, "POST", "/msg/newest_seq",                       handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/search_msg",                       handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/send_business_notification",       handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/pull_msg_by_seq",                  handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/mark_msgs_as_read",                handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/mark_conversation_as_read",        handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/get_conversations_has_read_and_max_seq", handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/set_conversation_has_read_seq",    handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/clear_conversation_msg",           handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/user_clear_all_msg",               handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/delete_msg_phsical_by_seq",        handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/delete_msg_physical",              handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/send_simple_msg",                  handle_msg, ctx);
    miku_http_server_route(srv, "POST", "/msg/check_msg_is_send_success",        handle_msg, ctx);

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

    miku_http_server_route(srv, "POST", "/third/fcm_update_token",           handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/set_app_badge",              handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/logs/upload",               handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/logs/delete",               handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/logs/search",               handle_third, ctx);
    miku_http_server_route(srv, "POST", "/third/prometheus",                handle_third, ctx);

    miku_http_server_route(srv, "POST", "/object/part_limit",                handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/part_size",                 handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/auth_sign",                 handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/initiate_form_data",        handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/complete_form_data",        handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/access_url",               handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/initiate_multipart_upload", handle_third, ctx);
    miku_http_server_route(srv, "POST", "/object/complete_multipart_upload", handle_third, ctx);

    /* Batch operations — 2 routes */
    miku_http_server_route(srv, "POST", "/batch/get_users_info",          handle_batch, ctx);
    miku_http_server_route(srv, "POST", "/batch/delete_friend",           handle_batch, ctx);

    /* Admin — 3 routes + version */
    miku_http_server_route(srv, "POST", "/admin/stats",     handle_admin, ctx);
    miku_http_server_route(srv, "GET",  "/admin/health",    handle_admin, ctx);
    miku_http_server_route(srv, "GET",  "/admin/metrics",   handle_metrics, ctx);
    miku_http_server_route(srv, "POST", "/admin/shutdown",  handle_admin, ctx);
    miku_http_server_route(srv, "GET",  "/version",         handle_version, ctx);

    miku_http_server_route(srv, "POST", "/statistics/user/register",  handle_statistics, ctx);
    miku_http_server_route(srv, "POST", "/statistics/user/active",    handle_statistics, ctx);
    miku_http_server_route(srv, "POST", "/statistics/group/create",   handle_statistics, ctx);
    miku_http_server_route(srv, "POST", "/statistics/group/active",   handle_statistics, ctx);

    miku_http_server_route(srv, "POST", "/jssdk/get_conversations",        handle_jssdk, ctx);
    miku_http_server_route(srv, "POST", "/jssdk/get_active_conversations", handle_jssdk, ctx);

    miku_http_server_route(srv, "GET",  "/prometheus_discovery/api",          handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/user",         handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/group",        handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/msg",          handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/friend",       handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/conversation", handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/third",        handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/auth",         handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/push",         handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/msg_gateway",  handle_prometheus_discovery, ctx);
    miku_http_server_route(srv, "GET",  "/prometheus_discovery/msg_transfer", handle_prometheus_discovery, ctx);

    miku_http_server_route(srv, "POST", "/config/get_config_list",              handle_config, ctx);
    miku_http_server_route(srv, "POST", "/config/get_config",                   handle_config, ctx);
    miku_http_server_route(srv, "POST", "/config/set_config",                   handle_config, ctx);
    miku_http_server_route(srv, "POST", "/config/reset_config",                 handle_config, ctx);
    miku_http_server_route(srv, "POST", "/config/set_enable_config_manager",    handle_config, ctx);
    miku_http_server_route(srv, "POST", "/config/get_enable_config_manager",    handle_config, ctx);

    miku_http_server_route(srv, "POST", "/restart", handle_restart, ctx);

    return 0;
}
