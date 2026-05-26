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
    else if (strstr(path, "update_user_info_ex")) method = "updateUserInfoEx";
    else if (strstr(path, "update_notification_account")) method = "updateNotificationAccount";
    else if (strstr(path, "update_user_status")) method = "updateUserStatus";
    else if (strstr(path, "update_user_info")) method = "updateUserInfo";
    else if (strstr(path, "get_users_online_token_detail")) method = "getUsersOnlineTokenDetail";
    else if (strstr(path, "get_users_online_status")) method = "getUsersOnlineStatus";
    else if (strstr(path, "get_users")) method = "getUsersInfo";
    else if (strstr(path, "account_check")) method = "accountCheck";
    else if (strstr(path, "get_all")) method = "getAllUsers";
    else if (strstr(path, "count")) method = "getUserCount";
    else if (strstr(path, "search_notification_account")) method = "searchNotificationAccount";
    else if (strstr(path, "search")) method = "searchUser";
    else if (strstr(path, "set_global_recv")) method = "setGlobalRecvMessageOpt";
    else if (strstr(path, "get_global_recv")) method = "getGlobalRecvMessageOpt";
    else if (strstr(path, "user_status")) method = "updateUserStatus";
    else if (strstr(path, "process_user_command_add")) method = "processUserCommandAdd";
    else if (strstr(path, "process_user_command_delete")) method = "processUserCommandDelete";
    else if (strstr(path, "process_user_command_update")) method = "processUserCommandUpdate";
    else if (strstr(path, "process_user_command_get_all")) method = "processUserCommandGetAll";
    else if (strstr(path, "process_user_command_get")) method = "processUserCommandGet";
    else if (strstr(path, "process_user_command")) method = "processUserCommand";
    else if (strstr(path, "get_user_status")) method = "getUserStatus";
    else if (strstr(path, "get_subscribe_users")) method = "getSubscribeUsersStatus";
    else if (strstr(path, "subscribe_or_cancel")) method = "subscribeOrCancelUserStatus";
    else if (strstr(path, "set_user_status")) method = "setUserStatus";
    else if (strstr(path, "get_all_users_uid")) method = "getAllUsersUID";
    else if (strstr(path, "add_notification_account")) method = "addNotificationAccount";
    else if (strstr(path, "get_user_client_config")) method = "getUserClientConfig";
    else if (strstr(path, "set_user_client_config")) method = "setUserClientConfig";
    else if (strstr(path, "del_user_client_config")) method = "delUserClientConfig";
    else if (strstr(path, "page_user_client_config")) method = "pageUserClientConfig";
    miku_user_handle_rpc(c->user, method, j, out);
    if (c->webhook && strcmp(method, "registerUser") == 0) {
        int64_t err = miku_json_int(miku_json_get(out, "errCode"));
        if (err == 0) {
            const char *uid = miku_json_str(miku_json_get(j, "userID"));
            char payload[256];
            snprintf(payload, sizeof(payload), "{\"event\":\"userRegistered\",\"userID\":\"%s\"}", uid ? uid : "");
            miku_webhook_fire(c->webhook, MK_WH_USER_ONLINE, payload);
        }
    }
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
    else if (strstr(path, "add_friend_response")) method = "respondFriendApply";
    else if (strstr(path, "add") && !strstr(path, "black")) method = "addFriend";
    else if (strstr(path, "remove_black")) method = "removeBlack";
    else if (strstr(path, "get_black")) method = "getBlackList";
    else if (strstr(path, "delete_friend")) method = "deleteFriend";
    else if (strstr(path, "delete") && !strstr(path, "friend")) method = "deleteFriend";
    else if (strstr(path, "is_friend")) method = "isFriend";
    else if (strstr(path, "get_designated_friends")) method = "getDesignatedFriends";
    else if (strstr(path, "get_designated")) method = "getDesignatedFriendApply";
    else if (strstr(path, "get_friend_apply")) method = "getFriendApplyList";
    else if (strstr(path, "get_self_unhandled_apply_count")) method = "getSelfUnhandledApplyCount";
    else if (strstr(path, "get_self_apply")) method = "getSelfApplyList";
    else if (strstr(path, "accept")) method = "acceptFriendApply";
    else if (strstr(path, "refuse")) method = "refuseFriendApply";
    else if (strstr(path, "import")) method = "importFriend";
    else if (strstr(path, "sync")) method = "syncFriend";
    else if (strstr(path, "set_friend_remark")) method = "setFriendRemark";
    else if (strstr(path, "get_specified_blacks")) method = "getSpecifiedBlacks";
    else if (strstr(path, "get_incremental_blacks")) method = "getIncrementalBlacks";
    else if (strstr(path, "get_incremental_friends")) method = "getIncrementalFriends";
    else if (strstr(path, "get_friend_id")) method = "getFriendIDs";
    else if (strstr(path, "get_specified_friends_info")) method = "getSpecifiedFriendsInfo";
    else if (strstr(path, "update_friends")) method = "updateFriends";
    else if (strstr(path, "get_full_friend_user_ids")) method = "getFullFriendUserIDs";
    miku_friend_handle_rpc(c->friend_svc, method, j, out);
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
    else if (strstr(path, "set_group_info_ex")) method = "setGroupInfoEx";
    else if (strstr(path, "set_group_info")) method = "setGroupInfo";
    else if (strstr(path, "set_group_member")) method = "setGroupMemberInfo";
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
    else if (strstr(path, "get_recv_group_application")) method = "getRecvGroupApplicationList";
    else if (strstr(path, "get_user_req_group_application")) method = "getUserReqGroupApplicationList";
    else if (strstr(path, "get_group_users_req")) method = "getGroupUsersReqApplicationList";
    else if (strstr(path, "get_specified_user_group_request")) method = "getSpecifiedUserGroupRequestInfo";
    else if (strstr(path, "get_group_abstract")) method = "getGroupAbstractInfo";
    else if (strstr(path, "get_groups") && !strstr(path, "info")) method = "getGroups";
    else if (strstr(path, "get_incremental_join_groups")) method = "getIncrementalJoinGroups";
    else if (strstr(path, "get_incremental_group_members_batch")) method = "getIncrementalGroupMemberBatch";
    else if (strstr(path, "get_incremental_group_members")) method = "getIncrementalGroupMembers";
    else if (strstr(path, "get_full_group_member")) method = "getFullGroupMemberUserIDs";
    else if (strstr(path, "get_full_join_group")) method = "getFullJoinGroupIDs";
    else if (strstr(path, "get_group_application_unhandled")) method = "getGroupApplicationUnhandledCount";
    miku_group_handle_rpc(c->group_svc, method, j, out);
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
    else if (strstr(path, "get_sorted_conversation")) method = "getSortedConversationList";
    else if (strstr(path, "get_full_conversation_ids")) method = "getFullConversationIDs";
    else if (strstr(path, "get_incremental_conversations")) method = "getIncrementalConversation";
    else if (strstr(path, "get_owner_conversation")) method = "getOwnerConversation";
    else if (strstr(path, "get_not_notify")) method = "getNotNotifyConversationIDs";
    else if (strstr(path, "get_pinned")) method = "getPinnedConversationIDs";
    else if (strstr(path, "delete_conversations")) method = "deleteConversations";
    else if (strstr(path, "update_conversations_by_user")) method = "updateConversationsByUser";
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
    else if (strstr(path, "send_business_notification")) method = "sendBusinessNotification";
    else if (strstr(path, "send_simple_msg")) method = "sendSimpleMsg";
    else if (strstr(path, "send_msg")) method = "sendMsg";
    else if (strstr(path, "batch_send")) method = "batchSendMsg";
    else if (strstr(path, "send_status")) method = "getSendMsgStatus";
    else if (strstr(path, "check_msg_is_send_success")) method = "checkMsgIsSendSuccess";
    else if (strstr(path, "send")) method = "send";
    else if (strstr(path, "delete_msg_phsical_by_seq")) method = "deleteMsgPhysicalBySeq";
    else if (strstr(path, "delete_msg_physical")) method = "deleteMsgPhysical";
    else if (strstr(path, "delete")) method = "deleteMsg";
    else if (strstr(path, "get_conversations_has_read")) method = "getConversationsHasReadAndMaxSeq";
    else if (strstr(path, "get_message_list_reaction")) method = "getMessageListReactionExtensions";
    else if (strstr(path, "get_msg")) method = "getMsg";
    else if (strstr(path, "get_by_seq")) method = "getMsgBySeq";
    else if (strstr(path, "pull_msg_by_seq")) method = "pullMsgBySeq";
    else if (strstr(path, "newest_seq")) method = "getNewestSeq";
    else if (strstr(path, "get")) method = "getMsgByConv";
    else if (strstr(path, "revoke")) method = "revokeMsg";
    else if (strstr(path, "clean")) method = "cleanUpMsg";
    else if (strstr(path, "mark_msgs_as_read")) method = "markMsgsAsRead";
    else if (strstr(path, "mark_conversation_as_read")) method = "markConversationAsRead";
    else if (strstr(path, "mark_as_read")) method = "markMsgAsRead";
    else if (strstr(path, "set_conversation_has_read")) method = "setConversationHasReadSeq";
    else if (strstr(path, "set_message_reaction")) method = "setMessageReactionExtensions";
    else if (strstr(path, "add_message_reaction")) method = "addMessageReactionExtensions";
    else if (strstr(path, "delete_message_reaction")) method = "deleteMessageReactionExtensions";
    else if (strstr(path, "search_msg")) method = "searchMsg";
    else if (strstr(path, "clear_conversation_msg")) method = "clearConversationMsg";
    else if (strstr(path, "user_clear_all_msg")) method = "userClearAllMsg";
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

    if (c->webhook) {
        int64_t wh_err = miku_json_int(miku_json_get(out, "errCode"));
        if (wh_err == 0 && strcmp(method, "sendMsg") == 0) {
            const char *sid = miku_json_str(miku_json_get(j, "sendID"));
            const char *rid = miku_json_str(miku_json_get(j, "recvID"));
            const char *smid = miku_json_str(miku_json_get(out, "serverMsgID"));
            char payload[1024];
            snprintf(payload, sizeof(payload), "{\"event\":\"msgSent\",\"sendID\":\"%s\",\"recvID\":\"%s\",\"serverMsgID\":\"%s\"}",
                     sid ? sid : "", rid ? rid : "", smid ? smid : "");
            miku_webhook_fire(c->webhook, MK_WH_AFTER_SEND_MSG, payload);
        } else if (wh_err == 0 && strcmp(method, "revokeMsg") == 0) {
            const char *cmid = miku_json_str(miku_json_get(j, "clientMsgID"));
            char payload[512];
            snprintf(payload, sizeof(payload), "{\"event\":\"msgRevoked\",\"clientMsgID\":\"%s\"}", cmid ? cmid : "");
            miku_webhook_fire(c->webhook, MK_WH_MSG_REVOKE, payload);
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
    else if (strstr(path, "fcm_update_token")) method = "fcmUpdateToken";
    else if (strstr(path, "set_app_badge")) method = "setAppBadge";
    else if (strstr(path, "upload") && strstr(path, "logs")) method = "uploadLogs";
    else if (strstr(path, "delete") && strstr(path, "logs")) method = "deleteLogs";
    else if (strstr(path, "search") && strstr(path, "logs")) method = "searchLogs";
    else if (strstr(path, "part_limit")) method = "partLimit";
    else if (strstr(path, "part_size")) method = "partSize";
    else if (strstr(path, "auth_sign")) method = "authSign";
    else if (strstr(path, "initiate_form_data")) method = "initiateFormData";
    else if (strstr(path, "complete_form_data")) method = "completeFormData";
    else if (strstr(path, "prometheus")) method = "getPrometheus";
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

static void handle_metrics(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req;
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
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

static void handle_statistics(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "userRegisterCount";
    if (strstr(path, "user") && strstr(path, "active")) method = "getActiveUser";
    else if (strstr(path, "user") && strstr(path, "register")) method = "userRegisterCount";
    else if (strstr(path, "group") && strstr(path, "active")) method = "getActiveGroup";
    else if (strstr(path, "group") && strstr(path, "create")) method = "groupCreateCount";
    miku_ji(out, "errCode", 0);
    miku_jss(out, "method", method);
    miku_ji(out, "count", 0);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_jssdk(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    miku_api_ctx_t *c = (miku_api_ctx_t *)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getConversations";
    if (strstr(path, "get_active")) method = "getActiveConversations";
    miku_conv_handle_rpc(c->conv, method, j, out);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_prometheus_discovery(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req; (void)ctx;
    miku_json_val_t *out = miku_json_create_object();
    miku_json_val_t *targets = miku_json_create_array();
    miku_json_val_t *item = miku_json_create_object();
    miku_jss(item, "targets", "127.0.0.1:10002");
    miku_json_array_push(targets, item);
    miku_json_object_set(out, "targets", targets);
    json_resp(resp, out);
}

static void handle_config(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)ctx;
    miku_json_val_t *j = parse_body(req);
    miku_json_val_t *out = miku_json_create_object();
    char *path = strndup(req->path.data, req->path.len);
    const char *method = "getConfigList";
    if (strstr(path, "get_config_list")) method = "getConfigList";
    else if (strstr(path, "get_config") && !strstr(path, "list") && !strstr(path, "enable")) method = "getConfig";
    else if (strstr(path, "set_config") && !strstr(path, "enable")) method = "setConfig";
    else if (strstr(path, "reset_config")) method = "resetConfig";
    else if (strstr(path, "set_enable_config")) method = "setEnableConfigManager";
    else if (strstr(path, "get_enable_config")) method = "getEnableConfigManager";
    miku_ji(out, "errCode", 0);
    miku_jss(out, "method", method);
    free(path);
    miku_json_destroy(j);
    json_resp(resp, out);
}

static void handle_restart(miku_http_request_t *req, miku_http_response_t *resp, void *ctx) {
    (void)req; (void)ctx;
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
