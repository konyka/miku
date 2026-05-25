#include "miku_friend.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_friend_service_s {
    miku_friend_t friends[MK_MAX_FRIENDS];
    int count;
};

miku_friend_service_t *miku_friend_service_create(void) {
    return (miku_friend_service_t *)calloc(1, sizeof(miku_friend_service_t));
}

void miku_friend_service_destroy(miku_friend_service_t *svc) { free(svc); }

int miku_friend_add(miku_friend_service_t *svc, const char *owner, const char *fuid, const char *remark) {
    if (!svc || !owner || !fuid || svc->count >= MK_MAX_FRIENDS) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->friends[i].owner_user_id, owner) == 0 &&
            strcmp(svc->friends[i].friend_user_id, fuid) == 0) return -2;
    }
    miku_friend_t *f = &svc->friends[svc->count++];
    strncpy(f->owner_user_id, owner, sizeof(f->owner_user_id) - 1);
    strncpy(f->friend_user_id, fuid, sizeof(f->friend_user_id) - 1);
    if (remark) strncpy(f->remark, remark, sizeof(f->remark) - 1);
    f->create_time = miku_timestamp_ms();
    return 0;
}

int miku_friend_delete(miku_friend_service_t *svc, const char *owner, const char *fuid) {
    if (!svc || !owner || !fuid) return -1;
    for (int i = 0; i < svc->count; i++) {
        if (strcmp(svc->friends[i].owner_user_id, owner) == 0 &&
            strcmp(svc->friends[i].friend_user_id, fuid) == 0) {
            memmove(&svc->friends[i], &svc->friends[i+1], (size_t)(svc->count-i-1) * sizeof(miku_friend_t));
            svc->count--;
            return 0;
        }
    }
    return -2;
}

int miku_friend_get_list(miku_friend_service_t *svc, const char *owner, miku_friend_t *out, int max) {
    if (!svc || !owner || !out) return 0;
    int n = 0;
    for (int i = 0; i < svc->count && n < max; i++) {
        if (strcmp(svc->friends[i].owner_user_id, owner) == 0)
            out[n++] = svc->friends[i];
    }
    return n;
}

bool miku_friend_is_friend(miku_friend_service_t *svc, const char *uid1, const char *uid2) {
    if (!svc || !uid1 || !uid2) return false;
    for (int i = 0; i < svc->count; i++) {
        if ((strcmp(svc->friends[i].owner_user_id, uid1) == 0 && strcmp(svc->friends[i].friend_user_id, uid2) == 0) ||
            (strcmp(svc->friends[i].owner_user_id, uid2) == 0 && strcmp(svc->friends[i].friend_user_id, uid1) == 0))
            return true;
    }
    return false;
}


void miku_friend_handle_rpc(miku_friend_service_t *svc, const char *method,
                             const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "addFriend") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *fuid = req ? miku_json_str(miku_json_get(req, "friendUserID")) : NULL;
        const char *rem = req ? miku_json_str(miku_json_get(req, "remark")) : NULL;
        int rc = miku_friend_add(svc, owner, fuid, rem);
        miku_ji(resp, "errCode", rc == 0 ? 0 : (rc == -2 ? 2001 : 500));
    } else if (strcmp(method, "deleteFriend") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *fuid = req ? miku_json_str(miku_json_get(req, "friendUserID")) : NULL;
        int rc = miku_friend_delete(svc, owner, fuid);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 2002);
    } else if (strcmp(method, "getFriendList") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_friend_t list[256];
        int n = miku_friend_get_list(svc, owner, list, 256);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) {
            miku_json_val_t *fj = miku_json_create_object();
            miku_jss(fj, "ownerUserID", list[i].owner_user_id);
            miku_jss(fj, "friendUserID", list[i].friend_user_id);
            miku_jss(fj, "remark", list[i].remark);
            miku_ji(fj, "createTime", list[i].create_time);
            miku_json_array_push(arr, fj);
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "isFriend") == 0) {
        const char *u1 = req ? miku_json_str(miku_json_get(req, "userID1")) : NULL;
        const char *u2 = req ? miku_json_str(miku_json_get(req, "userID2")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "isFriend", miku_friend_is_friend(svc, u1, u2) ? 1 : 0);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
