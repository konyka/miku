#include "miku_user.h"
#include "miku_hash.h"
#include "miku_log.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

/* 2x max users for open-addressing load factor ~0.5 */
#define MK_USER_HASH 8192

struct miku_user_service_s {
    miku_user_t  users[MK_MAX_USERS];
    int          count;
    int16_t      id_hash[MK_USER_HASH]; /* -1 empty, else users[] index */
};

static uint32_t user_hash_slot(const char *user_id) {
    return (uint32_t)(miku_fnv1a_64(user_id, strlen(user_id)) & (MK_USER_HASH - 1));
}

static void user_hash_insert(miku_user_service_t *svc, int ui) {
    uint32_t idx = user_hash_slot(svc->users[ui].user_id);
    for (int n = 0; n < MK_USER_HASH; n++) {
        if (svc->id_hash[idx] < 0) {
            svc->id_hash[idx] = (int16_t)ui;
            return;
        }
        idx = (idx + 1) & (MK_USER_HASH - 1);
    }
}

miku_user_service_t *miku_user_service_create(void) {
    miku_user_service_t *svc = (miku_user_service_t *)calloc(1, sizeof(*svc));
    if (svc) {
        for (int i = 0; i < MK_USER_HASH; i++) svc->id_hash[i] = -1;
    }
    return svc;
}

void miku_user_service_destroy(miku_user_service_t *svc) {
    free(svc);
}

int miku_user_register(miku_user_service_t *svc, const miku_user_t *user) {
    if (!svc || !user || svc->count >= MK_MAX_USERS) return -1;
    if (miku_user_find(svc, user->user_id)) return -2;
    int ui = svc->count++;
    svc->users[ui] = *user;
    svc->users[ui].create_time = miku_timestamp_ms();
    user_hash_insert(svc, ui);
    return 0;
}

miku_user_t *miku_user_find(miku_user_service_t *svc, const char *user_id) {
    if (!svc || !user_id) return NULL;
    uint32_t idx = user_hash_slot(user_id);
    for (int n = 0; n < MK_USER_HASH; n++) {
        int ui = svc->id_hash[idx];
        if (ui < 0) return NULL;
        if (strcmp(svc->users[ui].user_id, user_id) == 0)
            return &svc->users[ui];
        idx = (idx + 1) & (MK_USER_HASH - 1);
    }
    return NULL;
}

int miku_user_update(miku_user_service_t *svc, const miku_user_t *user) {
    if (!svc || !user) return -1;
    miku_user_t *existing = miku_user_find(svc, user->user_id);
    if (!existing) return -2;
    *existing = *user;
    existing->update_time = miku_timestamp_ms();
    return 0;
}

int miku_user_get_users(miku_user_service_t *svc, const char **user_ids, int count,
                         miku_user_t *out, int max_out) {
    if (!svc || !user_ids || !out) return 0;
    int found = 0;
    for (int i = 0; i < count && found < max_out; i++) {
        miku_user_t *u = miku_user_find(svc, user_ids[i]);
        if (u) out[found++] = *u;
    }
    return found;
}

int miku_user_count(miku_user_service_t *svc) {
    return svc ? svc->count : 0;
}

enum {
    MK_USER_RPC_registerUser = 0,
    MK_USER_RPC_getUserInfo = 1,
    MK_USER_RPC_updateUserInfo = 2,
    MK_USER_RPC_getUsersInfo = 3,
    MK_USER_RPC_accountCheck = 4,
    MK_USER_RPC_getAllUsers = 5,
    MK_USER_RPC_getUserCount = 6,
    MK_USER_RPC_searchUser = 7,
    MK_USER_RPC_getUsersOnlineStatus = 8,
    MK_USER_RPC_setGlobalRecvMessageOpt = 9,
    MK_USER_RPC_getGlobalRecvMessageOpt = 10,
    MK_USER_RPC_updateUserStatus = 11,
    MK_USER_RPC_getUserStatus = 12,
    MK_USER_RPC_setUserStatus = 13,
    MK_USER_RPC_getSubscribeUsersStatus = 14,
    MK_USER_RPC_subscribeOrCancelUserStatus = 15,
    MK_USER_RPC_updateUserInfoEx = 16,
    MK_USER_RPC_getAllUsersUID = 17,
    MK_USER_RPC_getUsersOnlineTokenDetail = 18,
    MK_USER_RPC_addNotificationAccount = 19,
    MK_USER_RPC_updateNotificationAccount = 20,
    MK_USER_RPC_searchNotificationAccount = 21,
    MK_USER_RPC_setUserClientConfig = 22,
    MK_USER_RPC_getUserClientConfig = 23,
    MK_USER_RPC_delUserClientConfig = 24,
    MK_USER_RPC_pageUserClientConfig = 25,
    MK_USER_RPC_processUserCommand = 26,
    MK_USER_RPC_processUserCommandAdd = 27,
    MK_USER_RPC_processUserCommandDelete = 28,
    MK_USER_RPC_processUserCommandUpdate = 29,
    MK_USER_RPC_processUserCommandGet = 30,
    MK_USER_RPC_processUserCommandGetAll = 31,
    MK_USER_RPC_COUNT = 32
};

#define MK_USER_RPC_HASH 64
static const char *const g_user_rpc_names[MK_USER_RPC_COUNT] = {
    "registerUser",
    "getUserInfo",
    "updateUserInfo",
    "getUsersInfo",
    "accountCheck",
    "getAllUsers",
    "getUserCount",
    "searchUser",
    "getUsersOnlineStatus",
    "setGlobalRecvMessageOpt",
    "getGlobalRecvMessageOpt",
    "updateUserStatus",
    "getUserStatus",
    "setUserStatus",
    "getSubscribeUsersStatus",
    "subscribeOrCancelUserStatus",
    "updateUserInfoEx",
    "getAllUsersUID",
    "getUsersOnlineTokenDetail",
    "addNotificationAccount",
    "updateNotificationAccount",
    "searchNotificationAccount",
    "setUserClientConfig",
    "getUserClientConfig",
    "delUserClientConfig",
    "pageUserClientConfig",
    "processUserCommand",
    "processUserCommandAdd",
    "processUserCommandDelete",
    "processUserCommandUpdate",
    "processUserCommandGet",
    "processUserCommandGetAll"
};

static int16_t g_user_rpc_hash[MK_USER_RPC_HASH];
static int g_user_rpc_ready;

static void user_rpc_init(void) {
    if (g_user_rpc_ready) return;
    for (int i = 0; i < MK_USER_RPC_HASH; i++) g_user_rpc_hash[i] = -1;
    for (int i = 0; i < MK_USER_RPC_COUNT; i++) {
        const char *m = g_user_rpc_names[i];
        uint32_t idx = (uint32_t)(miku_fnv1a_64(m, strlen(m)) & (MK_USER_RPC_HASH - 1));
        for (int n = 0; n < MK_USER_RPC_HASH; n++) {
            if (g_user_rpc_hash[idx] < 0) { g_user_rpc_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_USER_RPC_HASH - 1);
        }
    }
    g_user_rpc_ready = 1;
}

static int user_rpc_id(const char *method) {
    if (!method) return -1;
    user_rpc_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(method, strlen(method)) & (MK_USER_RPC_HASH - 1));
    for (int n = 0; n < MK_USER_RPC_HASH; n++) {
        int id = g_user_rpc_hash[idx];
        if (id < 0) return -1;
        if (strcmp(g_user_rpc_names[id], method) == 0) return id;
        idx = (idx + 1) & (MK_USER_RPC_HASH - 1);
    }
    return -1;
}

void miku_user_handle_rpc(miku_user_service_t *svc, const char *method,
                           const miku_json_val_t *req_json,
                           miku_json_val_t *resp_json) {
    if (!svc || !method || !resp_json) return;
    switch (user_rpc_id(method)) {
    case MK_USER_RPC_registerUser:
    {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        int rc = miku_user_register(svc, &u);
        miku_ji(resp_json, "errCode", rc == 0 ? 0 : (rc == -2 ? 1002 : 500));
        miku_jss(resp_json, "errMsg", rc == 0 ? "" : (rc == -2 ? "user exists" : "register failed"));
    } break;
    case MK_USER_RPC_getUserInfo:
    {
        miku_json_val_t *uid_v = req_json ? miku_json_get(req_json, "userID") : NULL;
        const char *uid = uid_v ? miku_json_str(uid_v) : NULL;
        miku_user_t *u = uid ? miku_user_find(svc, uid) : NULL;
        miku_ji(resp_json, "errCode", u ? 0 : 1001);
        if (u) {
            miku_json_val_t *uj = miku_user_to_json(u);
            miku_json_object_set(resp_json, "data", uj);
        }
    } break;
    case MK_USER_RPC_updateUserInfo:
    {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        int rc = miku_user_update(svc, &u);
        miku_ji(resp_json, "errCode", rc == 0 ? 0 : 1001);
    } break;
    case MK_USER_RPC_getUsersInfo:
    {
        miku_json_val_t *arr = req_json ? miku_json_get(req_json, "userIDList") : NULL;
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *result = miku_json_create_array();
        if (arr && miku_json_type(arr) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(arr);
            for (size_t i = 0; i < n; i++) {
                miku_json_val_t *iv = miku_json_at(arr, i);
                const char *uid = iv ? miku_json_str(iv) : NULL;
                miku_user_t *u = uid ? miku_user_find(svc, uid) : NULL;
                if (u) miku_json_array_push(result, miku_user_to_json(u));
            }
        }
        miku_json_object_set(resp_json, "data", result);
    } break;
    case MK_USER_RPC_accountCheck:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", miku_json_create_array());
    } break;
    case MK_USER_RPC_getAllUsers:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < svc->count; i++)
            miku_json_array_push(arr, miku_user_to_json(&svc->users[i]));
        miku_json_object_set(resp_json, "data", arr);
    } break;
    case MK_USER_RPC_getUserCount:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_ji(resp_json, "count", svc->count);
    } break;
    case MK_USER_RPC_searchUser:
    {
        const char *kw = req_json ? miku_json_str(miku_json_get(req_json, "keyword")) : NULL;
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (kw) {
            for (int i = 0; i < svc->count; i++) {
                if (strstr(svc->users[i].nickname, kw) ||
                    strstr(svc->users[i].user_id, kw))
                    miku_json_array_push(arr, miku_user_to_json(&svc->users[i]));
            }
        }
        miku_json_object_set(resp_json, "data", arr);
    } break;
    case MK_USER_RPC_getUsersOnlineStatus:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", miku_json_create_array());
    } break;
    case MK_USER_RPC_setGlobalRecvMessageOpt:
    case MK_USER_RPC_getGlobalRecvMessageOpt:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_updateUserStatus:
    case MK_USER_RPC_getUserStatus:
    case MK_USER_RPC_setUserStatus:
    case MK_USER_RPC_getSubscribeUsersStatus:
    case MK_USER_RPC_subscribeOrCancelUserStatus:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_updateUserInfoEx:
    {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        miku_user_t *existing = miku_user_find(svc, u.user_id);
        if (existing) { *existing = u; existing->update_time = miku_timestamp_ms(); }
        miku_ji(resp_json, "errCode", existing ? 0 : 1001);
    } break;
    case MK_USER_RPC_getAllUsersUID:
    {
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < svc->count; i++)
            miku_json_array_push(arr, miku_json_create_str(svc->users[i].user_id));
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", arr);
    } break;
    case MK_USER_RPC_getUsersOnlineTokenDetail:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_addNotificationAccount:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_updateNotificationAccount:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_searchNotificationAccount:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } break;
    case MK_USER_RPC_setUserClientConfig:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_getUserClientConfig:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_delUserClientConfig:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_pageUserClientConfig:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } break;
    case MK_USER_RPC_processUserCommand:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_processUserCommandAdd:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_processUserCommandDelete:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_processUserCommandUpdate:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_processUserCommandGet:
    {
        miku_ji(resp_json, "errCode", 0);
    } break;
    case MK_USER_RPC_processUserCommandGetAll:
    {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } break;
    default:
        miku_ji(resp_json, "errCode", 404);
        miku_jss(resp_json, "errMsg", "method not found");
        break;
    }
}

