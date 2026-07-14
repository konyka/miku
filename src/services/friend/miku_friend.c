#include "miku_friend.h"
#include "miku_hash.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

/* 2x max friends for open-addressing load factor ~0.5 */
#define MK_FRIEND_HASH 16384

struct miku_friend_service_s {
    miku_friend_t friends[MK_MAX_FRIENDS];
    int count;
    int16_t pair_hash[MK_FRIEND_HASH];  /* -1 empty, else friends[] index */
    int16_t owner_hash[MK_FRIEND_HASH]; /* owner → first friend index */
    int16_t owner_next[MK_MAX_FRIENDS]; /* intrusive list per owner */
};

static uint32_t pair_hash_slot(const char *owner, const char *fuid) {
    uint64_t a = miku_fnv1a_64(owner, strlen(owner));
    uint64_t b = miku_fnv1a_64(fuid, strlen(fuid));
    return (uint32_t)((a ^ (b * 0x9e3779b97f4a7c15ULL)) & (MK_FRIEND_HASH - 1));
}

static uint32_t owner_slot(const char *owner) {
    return (uint32_t)(miku_fnv1a_64(owner, strlen(owner)) & (MK_FRIEND_HASH - 1));
}

static void pair_hash_insert(miku_friend_service_t *svc, int fi) {
    uint32_t idx = pair_hash_slot(svc->friends[fi].owner_user_id,
                                  svc->friends[fi].friend_user_id);
    for (int n = 0; n < MK_FRIEND_HASH; n++) {
        if (svc->pair_hash[idx] < 0) {
            svc->pair_hash[idx] = (int16_t)fi;
            return;
        }
        idx = (idx + 1) & (MK_FRIEND_HASH - 1);
    }
}

static void owner_link(miku_friend_service_t *svc, int fi) {
    const char *owner = svc->friends[fi].owner_user_id;
    uint32_t idx = owner_slot(owner);
    for (int n = 0; n < MK_FRIEND_HASH; n++) {
        int head = svc->owner_hash[idx];
        if (head < 0) {
            svc->owner_hash[idx] = (int16_t)fi;
            svc->owner_next[fi] = -1;
            return;
        }
        if (strcmp(svc->friends[head].owner_user_id, owner) == 0) {
            svc->owner_next[fi] = (int16_t)head;
            svc->owner_hash[idx] = (int16_t)fi;
            return;
        }
        idx = (idx + 1) & (MK_FRIEND_HASH - 1);
    }
}

static int owner_head(miku_friend_service_t *svc, const char *owner) {
    uint32_t idx = owner_slot(owner);
    for (int n = 0; n < MK_FRIEND_HASH; n++) {
        int head = svc->owner_hash[idx];
        if (head < 0) return -1;
        if (strcmp(svc->friends[head].owner_user_id, owner) == 0) return head;
        idx = (idx + 1) & (MK_FRIEND_HASH - 1);
    }
    return -1;
}

static void indexes_rebuild(miku_friend_service_t *svc) {
    for (int i = 0; i < MK_FRIEND_HASH; i++) {
        svc->pair_hash[i] = -1;
        svc->owner_hash[i] = -1;
    }
    for (int i = 0; i < MK_MAX_FRIENDS; i++) svc->owner_next[i] = -1;
    for (int i = 0; i < svc->count; i++) {
        pair_hash_insert(svc, i);
        owner_link(svc, i);
    }
}

static int pair_hash_find(miku_friend_service_t *svc, const char *owner, const char *fuid) {
    uint32_t idx = pair_hash_slot(owner, fuid);
    for (int n = 0; n < MK_FRIEND_HASH; n++) {
        int fi = svc->pair_hash[idx];
        if (fi < 0) return -1;
        if (strcmp(svc->friends[fi].owner_user_id, owner) == 0 &&
            strcmp(svc->friends[fi].friend_user_id, fuid) == 0)
            return fi;
        idx = (idx + 1) & (MK_FRIEND_HASH - 1);
    }
    return -1;
}

miku_friend_service_t *miku_friend_service_create(void) {
    miku_friend_service_t *svc = (miku_friend_service_t *)calloc(1, sizeof(*svc));
    if (svc) {
        for (int i = 0; i < MK_FRIEND_HASH; i++) {
            svc->pair_hash[i] = -1;
            svc->owner_hash[i] = -1;
        }
        for (int i = 0; i < MK_MAX_FRIENDS; i++) svc->owner_next[i] = -1;
    }
    return svc;
}

void miku_friend_service_destroy(miku_friend_service_t *svc) { free(svc); }

int miku_friend_add(miku_friend_service_t *svc, const char *owner, const char *fuid, const char *remark) {
    if (!svc || !owner || !fuid || svc->count >= MK_MAX_FRIENDS) return -1;
    if (pair_hash_find(svc, owner, fuid) >= 0) return -2;
    int fi = svc->count++;
    miku_friend_t *f = &svc->friends[fi];
    memset(f, 0, sizeof(*f));
    strncpy(f->owner_user_id, owner, sizeof(f->owner_user_id) - 1);
    strncpy(f->friend_user_id, fuid, sizeof(f->friend_user_id) - 1);
    if (remark) strncpy(f->remark, remark, sizeof(f->remark) - 1);
    f->create_time = miku_timestamp_ms();
    pair_hash_insert(svc, fi);
    owner_link(svc, fi);
    return 0;
}

int miku_friend_delete(miku_friend_service_t *svc, const char *owner, const char *fuid) {
    if (!svc || !owner || !fuid) return -1;
    int fi = pair_hash_find(svc, owner, fuid);
    if (fi < 0) return -2;
    memmove(&svc->friends[fi], &svc->friends[fi + 1],
            (size_t)(svc->count - fi - 1) * sizeof(miku_friend_t));
    svc->count--;
    indexes_rebuild(svc); /* delete is rare; keep lookups O(1)/O(list) */
    return 0;
}

int miku_friend_get_list(miku_friend_service_t *svc, const char *owner, miku_friend_t *out, int max) {
    if (!svc || !owner || !out) return 0;
    int n = 0;
    for (int fi = owner_head(svc, owner); fi >= 0 && n < max; fi = svc->owner_next[fi])
        out[n++] = svc->friends[fi];
    return n;
}

bool miku_friend_is_friend(miku_friend_service_t *svc, const char *uid1, const char *uid2) {
    if (!svc || !uid1 || !uid2) return false;
    return pair_hash_find(svc, uid1, uid2) >= 0 || pair_hash_find(svc, uid2, uid1) >= 0;
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
        miku_friend_t list[16];
        int n = miku_friend_get_list(svc, owner, list, 16);
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
    } else if (strcmp(method, "addBlack") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_ji(resp, "errCode", (owner && uid) ? 0 : 400);
    } else if (strcmp(method, "removeBlack") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_ji(resp, "errCode", (owner && uid) ? 0 : 400);
    } else if (strcmp(method, "getBlackList") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } else if (strcmp(method, "getFriendApplyList") == 0 ||
               strcmp(method, "getSelfApplyList") == 0 ||
               strcmp(method, "getDesignatedFriendApply") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } else if (strcmp(method, "acceptFriendApply") == 0 ||
               strcmp(method, "refuseFriendApply") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *fuid = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (strcmp(method, "acceptFriendApply") == 0 && owner && fuid) {
            miku_friend_add(svc, owner, fuid, NULL);
        }
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "importFriend") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *fl = req ? miku_json_get(req, "friendList") : NULL;
        int imported = 0;
        if (owner && fl && miku_json_type(fl) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(fl);
            for (size_t i = 0; i < n; i++) {
                const char *fuid = miku_json_str(miku_json_at(fl, i));
                if (fuid && miku_friend_add(svc, owner, fuid, NULL) == 0) imported++;
            }
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "imported", imported);
    } else if (strcmp(method, "syncFriend") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_friend_t list[16];
        int n = miku_friend_get_list(svc, owner, list, 16);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) {
            miku_json_val_t *fj = miku_json_create_object();
            miku_jss(fj, "friendUserID", list[i].friend_user_id);
            miku_json_array_push(arr, fj);
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getDesignatedFriends") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *ids = req ? miku_json_get(req, "friendUserIDs") : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (owner && ids && miku_json_type(ids) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(ids);
            for (size_t i = 0; i < n; i++) {
                const char *fuid = miku_json_str(miku_json_at(ids, i));
                if (fuid) {
                    int fi = pair_hash_find(svc, owner, fuid);
                    if (fi >= 0) {
                        miku_json_val_t *fj = miku_json_create_object();
                        miku_jss(fj, "ownerUserID", svc->friends[fi].owner_user_id);
                        miku_jss(fj, "friendUserID", svc->friends[fi].friend_user_id);
                        miku_jss(fj, "remark", svc->friends[fi].remark);
                        miku_json_array_push(arr, fj);
                    }
                }
            }
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getFriendIDs") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *arr = miku_json_create_array();
        if (owner) {
            for (int fi = owner_head(svc, owner); fi >= 0; fi = svc->owner_next[fi])
                miku_json_array_push(arr, miku_json_create_str(svc->friends[fi].friend_user_id));
        }
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getFullFriendUserIDs") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!owner) owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *arr = miku_json_create_array();
        if (owner) {
            for (int fi = owner_head(svc, owner); fi >= 0; fi = svc->owner_next[fi])
                miku_json_array_push(arr, miku_json_create_str(svc->friends[fi].friend_user_id));
        }
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getIncrementalFriends") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getIncrementalBlacks") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getSelfUnhandledApplyCount") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "count", 0);
    } else if (strcmp(method, "getSpecifiedBlacks") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getSpecifiedFriendsInfo") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *ids = req ? miku_json_get(req, "friendUserIDs") : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (owner && ids && miku_json_type(ids) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(ids);
            for (size_t i = 0; i < n; i++) {
                const char *fuid = miku_json_str(miku_json_at(ids, i));
                if (fuid) {
                    int fi = pair_hash_find(svc, owner, fuid);
                    if (fi >= 0) {
                        miku_json_val_t *fj = miku_json_create_object();
                        miku_jss(fj, "friendUserID", svc->friends[fi].friend_user_id);
                        miku_jss(fj, "remark", svc->friends[fi].remark);
                        miku_ji(fj, "createTime", (int)svc->friends[fi].create_time);
                        miku_json_array_push(arr, fj);
                    }
                }
            }
        }
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "respondFriendApply") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *fuid = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        const char *handle = req ? miku_json_str(miku_json_get(req, "handleMsg")) : NULL;
        (void)handle;
        if (owner && fuid) miku_friend_add(svc, owner, fuid, NULL);
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "setFriendRemark") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        const char *fuid = req ? miku_json_str(miku_json_get(req, "friendUserID")) : NULL;
        const char *remark = req ? miku_json_str(miku_json_get(req, "remark")) : NULL;
        int found = 0;
        if (owner && fuid) {
            int fi = pair_hash_find(svc, owner, fuid);
            if (fi >= 0) {
                if (remark) strncpy(svc->friends[fi].remark, remark, sizeof(svc->friends[fi].remark) - 1);
                found = 1;
            }
        }
        miku_ji(resp, "errCode", found ? 0 : 2002);
    } else if (strcmp(method, "updateFriends") == 0) {
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_json_val_t *fl = req ? miku_json_get(req, "friendList") : NULL;
        int updated = 0;
        if (owner && fl && miku_json_type(fl) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(fl);
            for (size_t i = 0; i < n; i++) {
                miku_json_val_t *item = miku_json_at(fl, i);
                const char *fuid = item ? miku_json_str(miku_json_get(item, "friendUserID")) : NULL;
                const char *rem = item ? miku_json_str(miku_json_get(item, "remark")) : NULL;
                if (fuid) {
                    int fi = pair_hash_find(svc, owner, fuid);
                    if (fi >= 0) {
                        if (rem) strncpy(svc->friends[fi].remark, rem, sizeof(svc->friends[fi].remark) - 1);
                        updated++;
                    }
                }
            }
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "updated", updated);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
