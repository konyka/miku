#include "miku_user.h"
#include "miku_log.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_user_service_s {
    miku_user_t  users[MK_MAX_USERS];
    int          count;
};

miku_user_service_t *miku_user_service_create(void) {
    miku_user_service_t *svc = (miku_user_service_t *)calloc(1, sizeof(*svc));
    return svc;
}

void miku_user_service_destroy(miku_user_service_t *svc) {
    free(svc);
}

int miku_user_register(miku_user_service_t *svc, const miku_user_t *user) {
    if (!svc || !user || svc->count >= MK_MAX_USERS) return -1;
    if (miku_user_find(svc, user->user_id)) return -2;
    svc->users[svc->count] = *user;
    svc->users[svc->count].create_time = miku_timestamp_ms();
    svc->count++;
    return 0;
}

miku_user_t *miku_user_find(miku_user_service_t *svc, const char *user_id) {
    if (!svc || !user_id) return NULL;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->users[i].user_id, user_id) == 0)
            return &svc->users[i];
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

void miku_user_handle_rpc(miku_user_service_t *svc, const char *method,
                           const miku_json_val_t *req_json,
                           miku_json_val_t *resp_json) {
    if (!svc || !method || !resp_json) return;

    if (strcmp(method, "registerUser") == 0) {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        int rc = miku_user_register(svc, &u);
        miku_ji(resp_json, "errCode", rc == 0 ? 0 : (rc == -2 ? 1002 : 500));
        miku_jss(resp_json, "errMsg", rc == 0 ? "" : (rc == -2 ? "user exists" : "register failed"));

    } else if (strcmp(method, "getUserInfo") == 0) {
        miku_json_val_t *uid_v = req_json ? miku_json_get(req_json, "userID") : NULL;
        const char *uid = uid_v ? miku_json_str(uid_v) : NULL;
        miku_user_t *u = uid ? miku_user_find(svc, uid) : NULL;
        miku_ji(resp_json, "errCode", u ? 0 : 1001);
        if (u) {
            miku_json_val_t *uj = miku_user_to_json(u);
            miku_json_object_set(resp_json, "data", uj);
        }

    } else if (strcmp(method, "updateUserInfo") == 0) {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        int rc = miku_user_update(svc, &u);
        miku_ji(resp_json, "errCode", rc == 0 ? 0 : 1001);

    } else if (strcmp(method, "getUsersInfo") == 0) {
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

    } else if (strcmp(method, "accountCheck") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", miku_json_create_array());

    } else if (strcmp(method, "getAllUsers") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < svc->count; i++)
            miku_json_array_push(arr, miku_user_to_json(&svc->users[i]));
        miku_json_object_set(resp_json, "data", arr);

    } else if (strcmp(method, "getUserCount") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_ji(resp_json, "count", svc->count);

    } else if (strcmp(method, "searchUser") == 0) {
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

    } else if (strcmp(method, "getUsersOnlineStatus") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", miku_json_create_array());

    } else if (strcmp(method, "setGlobalRecvMessageOpt") == 0 ||
               strcmp(method, "getGlobalRecvMessageOpt") == 0) {
        miku_ji(resp_json, "errCode", 0);

    } else if (strcmp(method, "updateUserStatus") == 0 ||
               strcmp(method, "getUserStatus") == 0 ||
               strcmp(method, "setUserStatus") == 0 ||
               strcmp(method, "getSubscribeUsersStatus") == 0 ||
               strcmp(method, "subscribeOrCancelUserStatus") == 0) {
        miku_ji(resp_json, "errCode", 0);

    } else if (strcmp(method, "updateUserInfoEx") == 0) {
        miku_user_t u;
        memset(&u, 0, sizeof(u));
        miku_user_from_json(req_json, &u);
        miku_user_t *existing = miku_user_find(svc, u.user_id);
        if (existing) { *existing = u; existing->update_time = miku_timestamp_ms(); }
        miku_ji(resp_json, "errCode", existing ? 0 : 1001);
    } else if (strcmp(method, "getAllUsersUID") == 0) {
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < svc->count; i++)
            miku_json_array_push(arr, miku_json_create_str(svc->users[i].user_id));
        miku_ji(resp_json, "errCode", 0);
        miku_json_object_set(resp_json, "data", arr);
    } else if (strcmp(method, "getUsersOnlineTokenDetail") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "addNotificationAccount") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "updateNotificationAccount") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "searchNotificationAccount") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } else if (strcmp(method, "setUserClientConfig") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "getUserClientConfig") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "delUserClientConfig") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "pageUserClientConfig") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } else if (strcmp(method, "processUserCommand") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "processUserCommandAdd") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "processUserCommandDelete") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "processUserCommandUpdate") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "processUserCommandGet") == 0) {
        miku_ji(resp_json, "errCode", 0);
    } else if (strcmp(method, "processUserCommandGetAll") == 0) {
        miku_ji(resp_json, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp_json, "data", arr);
    } else {
        miku_ji(resp_json, "errCode", 404);
        miku_jss(resp_json, "errMsg", "method not found");
    }
}
