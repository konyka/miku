#include "miku_group.h"
#include "miku_hash.h"
#include "miku_uuid.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

#define MK_GROUP_HASH  4096
#define MK_MEMBER_HASH 32768

struct miku_group_service_s {
    miku_group_t groups[MK_MAX_GROUPS];
    int group_count;
    miku_group_member_t members[MK_MAX_MEMBERS];
    int member_count;
    int16_t group_hash[MK_GROUP_HASH];   /* -1 empty, else groups[] index */
    int16_t member_hash[MK_MEMBER_HASH]; /* -1 empty, else members[] index */
    int16_t member_head[MK_MAX_GROUPS];  /* first member idx per group, -1 none */
    int16_t member_next[MK_MAX_MEMBERS]; /* intrusive list within a group */
};

static uint32_t group_hash_slot(const char *group_id) {
    return (uint32_t)(miku_fnv1a_64(group_id, strlen(group_id)) & (MK_GROUP_HASH - 1));
}

static uint32_t member_hash_slot(const char *group_id, const char *user_id) {
    uint64_t a = miku_fnv1a_64(group_id, strlen(group_id));
    uint64_t b = miku_fnv1a_64(user_id, strlen(user_id));
    return (uint32_t)((a ^ (b * 0x9e3779b97f4a7c15ULL)) & (MK_MEMBER_HASH - 1));
}

static void group_hash_insert(miku_group_service_t *svc, int gi) {
    uint32_t idx = group_hash_slot(svc->groups[gi].group_id);
    for (int n = 0; n < MK_GROUP_HASH; n++) {
        if (svc->group_hash[idx] < 0) {
            svc->group_hash[idx] = (int16_t)gi;
            return;
        }
        idx = (idx + 1) & (MK_GROUP_HASH - 1);
    }
}

static int group_index_find(miku_group_service_t *svc, const char *group_id) {
    if (!group_id) return -1;
    uint32_t idx = group_hash_slot(group_id);
    for (int n = 0; n < MK_GROUP_HASH; n++) {
        int gi = svc->group_hash[idx];
        if (gi < 0) return -1;
        if (strcmp(svc->groups[gi].group_id, group_id) == 0) return gi;
        idx = (idx + 1) & (MK_GROUP_HASH - 1);
    }
    return -1;
}

static void member_hash_insert(miku_group_service_t *svc, int mi) {
    uint32_t idx = member_hash_slot(svc->members[mi].group_id, svc->members[mi].user_id);
    for (int n = 0; n < MK_MEMBER_HASH; n++) {
        if (svc->member_hash[idx] < 0) {
            svc->member_hash[idx] = (int16_t)mi;
            return;
        }
        idx = (idx + 1) & (MK_MEMBER_HASH - 1);
    }
}

static int member_index_find(miku_group_service_t *svc, const char *group_id, const char *user_id) {
    uint32_t idx = member_hash_slot(group_id, user_id);
    for (int n = 0; n < MK_MEMBER_HASH; n++) {
        int mi = svc->member_hash[idx];
        if (mi < 0) return -1;
        if (strcmp(svc->members[mi].group_id, group_id) == 0 &&
            strcmp(svc->members[mi].user_id, user_id) == 0)
            return mi;
        idx = (idx + 1) & (MK_MEMBER_HASH - 1);
    }
    return -1;
}

/* Rebuild member hash + per-group chains (used after swap-remove). */
static void rebuild_member_indexes(miku_group_service_t *svc) {
    for (int i = 0; i < MK_MEMBER_HASH; i++) svc->member_hash[i] = -1;
    for (int i = 0; i < svc->group_count; i++) svc->member_head[i] = -1;
    for (int mi = 0; mi < svc->member_count; mi++) {
        svc->member_next[mi] = -1;
        member_hash_insert(svc, mi);
        int gi = group_index_find(svc, svc->members[mi].group_id);
        if (gi < 0) continue;
        svc->member_next[mi] = svc->member_head[gi];
        svc->member_head[gi] = (int16_t)mi;
    }
}

miku_group_service_t *miku_group_service_create(void) {
    miku_group_service_t *svc = (miku_group_service_t *)calloc(1, sizeof(*svc));
    if (svc) {
        for (int i = 0; i < MK_GROUP_HASH; i++) svc->group_hash[i] = -1;
        for (int i = 0; i < MK_MEMBER_HASH; i++) svc->member_hash[i] = -1;
        for (int i = 0; i < MK_MAX_GROUPS; i++) svc->member_head[i] = -1;
    }
    return svc;
}
void miku_group_service_destroy(miku_group_service_t *svc) { free(svc); }

int miku_group_create(miku_group_service_t *svc, miku_group_t *g, const char *owner_uid) {
    if (!svc || !g || !owner_uid || svc->group_count >= MK_MAX_GROUPS) return -1;
    miku_uuid_generate(g->group_id);
    strncpy(g->owner_user_id, owner_uid, sizeof(g->owner_user_id) - 1);
    g->create_time = miku_timestamp_ms();
    g->member_count = 0;
    g->status = 0;
    int gi = svc->group_count++;
    svc->groups[gi] = *g;
    svc->member_head[gi] = -1;
    group_hash_insert(svc, gi);
    miku_group_add_member(svc, g->group_id, owner_uid, 100);
    miku_group_t *stored = miku_group_find(svc, g->group_id);
    if (stored) g->member_count = stored->member_count;
    return 0;
}

miku_group_t *miku_group_find(miku_group_service_t *svc, const char *group_id) {
    if (!svc || !group_id) return NULL;
    int gi = group_index_find(svc, group_id);
    return gi >= 0 ? &svc->groups[gi] : NULL;
}

int miku_group_add_member(miku_group_service_t *svc, const char *group_id, const char *user_id, int role) {
    if (!svc || !group_id || !user_id || svc->member_count >= MK_MAX_MEMBERS) return -1;
    if (member_index_find(svc, group_id, user_id) >= 0) return 0; /* already a member */
    int gi = group_index_find(svc, group_id);
    int mi = svc->member_count++;
    miku_group_member_t *m = &svc->members[mi];
    memset(m, 0, sizeof(*m));
    strncpy(m->group_id, group_id, sizeof(m->group_id) - 1);
    strncpy(m->user_id, user_id, sizeof(m->user_id) - 1);
    m->role_level = role;
    m->join_time = miku_timestamp_ms();
    member_hash_insert(svc, mi);
    if (gi >= 0) {
        svc->member_next[mi] = svc->member_head[gi];
        svc->member_head[gi] = (int16_t)mi;
        svc->groups[gi].member_count++;
    } else {
        svc->member_next[mi] = -1;
    }
    return 0;
}

int miku_group_remove_member(miku_group_service_t *svc, const char *group_id, const char *user_id) {
    if (!svc || !group_id || !user_id) return -1;
    int mi = member_index_find(svc, group_id, user_id);
    if (mi < 0) return -1;
    svc->members[mi] = svc->members[svc->member_count - 1];
    svc->member_count--;
    miku_group_t *g = miku_group_find(svc, group_id);
    if (g && g->member_count > 0) g->member_count--;
    rebuild_member_indexes(svc); /* remove is rare; keep foreach O(group size) */
    return 0;
}

int miku_group_get_members(miku_group_service_t *svc, const char *group_id, miku_group_member_t *out, int max) {
    if (!svc || !group_id || !out) return 0;
    int gi = group_index_find(svc, group_id);
    if (gi >= 0) {
        int n = 0;
        for (int mi = svc->member_head[gi]; mi >= 0 && n < max; mi = svc->member_next[mi])
            out[n++] = svc->members[mi];
        return n;
    }
    /* Gateway may sync members without a local createGroup entry. */
    int n = 0;
    for (int i = 0; i < svc->member_count && n < max; i++)
        if (strcmp(svc->members[i].group_id, group_id) == 0) out[n++] = svc->members[i];
    return n;
}

int miku_group_foreach_member(miku_group_service_t *svc, const char *group_id,
                              miku_group_member_fn fn, void *ctx) {
    if (!svc || !group_id || !fn) return 0;
    int gi = group_index_find(svc, group_id);
    if (gi >= 0) {
        int n = 0;
        for (int mi = svc->member_head[gi]; mi >= 0; mi = svc->member_next[mi]) {
            fn(svc->members[mi].user_id, svc->members[mi].role_level, ctx);
            n++;
        }
        return n;
    }
    int n = 0;
    for (int i = 0; i < svc->member_count; i++) {
        if (strcmp(svc->members[i].group_id, group_id) != 0) continue;
        fn(svc->members[i].user_id, svc->members[i].role_level, ctx);
        n++;
    }
    return n;
}

int miku_group_is_member(miku_group_service_t *svc, const char *group_id,
                         const char *user_id) {
    if (!svc || !group_id || !user_id || !group_id[0] || !user_id[0]) return 0;
    return member_index_find(svc, group_id, user_id) >= 0;
}

int miku_group_member_role(miku_group_service_t *svc, const char *group_id,
                           const char *user_id) {
    if (!svc || !group_id || !user_id || !group_id[0] || !user_id[0]) return -1;
    int mi = member_index_find(svc, group_id, user_id);
    return mi >= 0 ? svc->members[mi].role_level : -1;
}


enum {
    MK_GROUP_RPC_createGroup = 0,
    MK_GROUP_RPC_getGroupInfo = 1,
    MK_GROUP_RPC_inviteToGroup = 2,
    MK_GROUP_RPC_getGroupMemberList = 3,
    MK_GROUP_RPC_getGroupsInfo = 4,
    MK_GROUP_RPC_setGroupInfo = 5,
    MK_GROUP_RPC_setGroupMemberInfo = 6,
    MK_GROUP_RPC_joinGroup = 7,
    MK_GROUP_RPC_quitGroup = 8,
    MK_GROUP_RPC_dismissGroup = 9,
    MK_GROUP_RPC_muteGroup = 10,
    MK_GROUP_RPC_cancelMuteGroup = 11,
    MK_GROUP_RPC_kickGroupMember = 12,
    MK_GROUP_RPC_transferGroupOwner = 13,
    MK_GROUP_RPC_getJoinedGroupList = 14,
    MK_GROUP_RPC_getGroupApplicationList = 15,
    MK_GROUP_RPC_getGroupApplicantList = 16,
    MK_GROUP_RPC_acceptGroupApplication = 17,
    MK_GROUP_RPC_refuseGroupApplication = 18,
    MK_GROUP_RPC_getGroupMemberUserID = 19,
    MK_GROUP_RPC_muteGroupMember = 20,
    MK_GROUP_RPC_cancelMuteGroupMember = 21,
    MK_GROUP_RPC_getFullGroupMemberUserIDs = 22,
    MK_GROUP_RPC_getFullJoinGroupIDs = 23,
    MK_GROUP_RPC_getGroupAbstractInfo = 24,
    MK_GROUP_RPC_getGroupApplicationUnhandledCount = 25,
    MK_GROUP_RPC_getGroupUsersReqApplicationList = 26,
    MK_GROUP_RPC_getGroups = 27,
    MK_GROUP_RPC_getIncrementalGroupMemberBatch = 28,
    MK_GROUP_RPC_getIncrementalGroupMembers = 29,
    MK_GROUP_RPC_getIncrementalJoinGroups = 30,
    MK_GROUP_RPC_getRecvGroupApplicationList = 31,
    MK_GROUP_RPC_getSpecifiedUserGroupRequestInfo = 32,
    MK_GROUP_RPC_getUserReqGroupApplicationList = 33,
    MK_GROUP_RPC_setGroupInfoEx = 34,
    MK_GROUP_RPC_COUNT = 35
};

#define MK_GROUP_RPC_HASH 64
static const char *const g_group_rpc_names[MK_GROUP_RPC_COUNT] = {
    "createGroup",
    "getGroupInfo",
    "inviteToGroup",
    "getGroupMemberList",
    "getGroupsInfo",
    "setGroupInfo",
    "setGroupMemberInfo",
    "joinGroup",
    "quitGroup",
    "dismissGroup",
    "muteGroup",
    "cancelMuteGroup",
    "kickGroupMember",
    "transferGroupOwner",
    "getJoinedGroupList",
    "getGroupApplicationList",
    "getGroupApplicantList",
    "acceptGroupApplication",
    "refuseGroupApplication",
    "getGroupMemberUserID",
    "muteGroupMember",
    "cancelMuteGroupMember",
    "getFullGroupMemberUserIDs",
    "getFullJoinGroupIDs",
    "getGroupAbstractInfo",
    "getGroupApplicationUnhandledCount",
    "getGroupUsersReqApplicationList",
    "getGroups",
    "getIncrementalGroupMemberBatch",
    "getIncrementalGroupMembers",
    "getIncrementalJoinGroups",
    "getRecvGroupApplicationList",
    "getSpecifiedUserGroupRequestInfo",
    "getUserReqGroupApplicationList",
    "setGroupInfoEx"
};

static int16_t g_group_rpc_hash[MK_GROUP_RPC_HASH];
static int g_group_rpc_ready;

static void group_rpc_init(void) {
    if (g_group_rpc_ready) return;
    for (int i = 0; i < MK_GROUP_RPC_HASH; i++) g_group_rpc_hash[i] = -1;
    for (int i = 0; i < MK_GROUP_RPC_COUNT; i++) {
        const char *m = g_group_rpc_names[i];
        uint32_t idx = (uint32_t)(miku_fnv1a_64(m, strlen(m)) & (MK_GROUP_RPC_HASH - 1));
        for (int n = 0; n < MK_GROUP_RPC_HASH; n++) {
            if (g_group_rpc_hash[idx] < 0) { g_group_rpc_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_GROUP_RPC_HASH - 1);
        }
    }
    g_group_rpc_ready = 1;
}

static int group_rpc_id(const char *method) {
    if (!method) return -1;
    group_rpc_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(method, strlen(method)) & (MK_GROUP_RPC_HASH - 1));
    for (int n = 0; n < MK_GROUP_RPC_HASH; n++) {
        int id = g_group_rpc_hash[idx];
        if (id < 0) return -1;
        if (strcmp(g_group_rpc_names[id], method) == 0) return id;
        idx = (idx + 1) & (MK_GROUP_RPC_HASH - 1);
    }
    return -1;
}

void miku_group_handle_rpc(miku_group_service_t *svc, const char *method,
                            const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    switch (group_rpc_id(method)) {
    case MK_GROUP_RPC_createGroup:
    {
        miku_group_t g;
        memset(&g, 0, sizeof(g));
        const char *name = req ? miku_json_str(miku_json_get(req, "groupName")) : NULL;
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (name) strncpy(g.group_name, name, sizeof(g.group_name) - 1);
        int rc = miku_group_create(svc, &g, owner);
        if (rc == 0) {
            miku_json_val_t *ids = req ? miku_json_get(req, "memberUserIDs") : NULL;
            if (!ids) ids = req ? miku_json_get(req, "invitedUserIDs") : NULL;
            if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
                size_t n = miku_json_size(ids);
                for (size_t i = 0; i < n; i++) {
                    const char *u = miku_json_str(miku_json_at(ids, i));
                    if (u && owner && strcmp(u, owner) != 0)
                        miku_group_add_member(svc, g.group_id, u, 20);
                }
            }
        }
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) miku_jss(resp, "data", g.group_id);
    } break;
    case MK_GROUP_RPC_getGroupInfo:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_group_t *g = miku_group_find(svc, gid);
        miku_ji(resp, "errCode", g ? 0 : 3001);
        if (g) miku_json_object_set(resp, "data", miku_group_to_json(g));
    } break;
    case MK_GROUP_RPC_inviteToGroup:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *from = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (!from || !from[0]) from = req ? miku_json_str(miku_json_get(req, "opUserID")) : NULL;
        if (!gid || !from || !from[0] || miku_group_member_role(svc, gid, from) < 20) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        int rc = -1;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (uid)
            rc = miku_group_add_member(svc, gid, uid, 20);
        miku_json_val_t *ids = req ? miku_json_get(req, "invitedUserIDs") : NULL;
        if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(ids);
            for (size_t i = 0; i < n; i++) {
                const char *u = miku_json_str(miku_json_at(ids, i));
                if (u && miku_group_add_member(svc, gid, u, 20) == 0)
                    rc = 0;
            }
        }
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } break;
    case MK_GROUP_RPC_getGroupMemberList:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (gid) {
            int gi = group_index_find(svc, gid);
            if (gi >= 0) {
                for (int mi = svc->member_head[gi]; mi >= 0; mi = svc->member_next[mi])
                    miku_json_array_push(arr, miku_group_member_to_json(&svc->members[mi]));
            } else {
                for (int i = 0; i < svc->member_count; i++)
                    if (strcmp(svc->members[i].group_id, gid) == 0)
                        miku_json_array_push(arr, miku_group_member_to_json(&svc->members[i]));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getGroupsInfo:
    {
        miku_json_val_t *ids = req ? miku_json_get(req, "groupIDList") : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(ids);
            for (size_t i = 0; i < n; i++) {
                const char *gid = miku_json_str(miku_json_at(ids, i));
                miku_group_t *g = miku_group_find(svc, gid);
                if (g) miku_json_array_push(arr, miku_group_to_json(g));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_setGroupInfo:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *op = req ? miku_json_str(miku_json_get(req, "opUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_group_t *g = gid ? miku_group_find(svc, gid) : NULL;
        if (!g) {
            miku_ji(resp, "errCode", 3001);
            break;
        }
        if (!op || !op[0] || miku_group_member_role(svc, gid, op) < 60) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        const char *name = req ? miku_json_str(miku_json_get(req, "groupName")) : NULL;
        if (name) strncpy(g->group_name, name, sizeof(g->group_name) - 1);
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_setGroupMemberInfo:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_joinGroup:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int rc = miku_group_add_member(svc, gid, uid, 20);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } break;
    case MK_GROUP_RPC_quitGroup:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int rc = miku_group_remove_member(svc, gid, uid);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } break;
    case MK_GROUP_RPC_dismissGroup:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *op = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        miku_group_t *g = gid ? miku_group_find(svc, gid) : NULL;
        if (!g) {
            miku_ji(resp, "errCode", 3001);
            break;
        }
        if (!op || !op[0] || strcmp(op, g->owner_user_id) != 0) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        int w = 0;
        for (int i = 0; i < svc->member_count; i++) {
            if (strcmp(svc->members[i].group_id, gid) == 0) continue;
            svc->members[w++] = svc->members[i];
        }
        svc->member_count = w;
        g->member_count = 0;
        g->status = 2; /* dismissed */
        rebuild_member_indexes(svc);
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_muteGroup:
    case MK_GROUP_RPC_cancelMuteGroup:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_kickGroupMember:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        /* userID is the kicked member; operator is op/from/ownerUserID. */
        const char *op = req ? miku_json_str(miku_json_get(req, "opUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (!gid || !op || !op[0] || miku_group_member_role(svc, gid, op) < 60) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        int rc = -1;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (uid && strcmp(uid, op) != 0)
            rc = miku_group_remove_member(svc, gid, uid);
        miku_json_val_t *ids = req ? miku_json_get(req, "kickedUserIDs") : NULL;
        if (!ids) ids = req ? miku_json_get(req, "invitedUserIDs") : NULL;
        if (ids && miku_json_type(ids) == MK_JSON_ARRAY) {
            size_t n = miku_json_size(ids);
            for (size_t i = 0; i < n; i++) {
                const char *u = miku_json_str(miku_json_at(ids, i));
                if (u && strcmp(u, op) != 0 && miku_group_remove_member(svc, gid, u) == 0)
                    rc = 0;
            }
        }
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } break;
    case MK_GROUP_RPC_transferGroupOwner:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *op = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "opUserID")) : NULL;
        const char *new_owner = req ? miku_json_str(miku_json_get(req, "newOwnerUserID")) : NULL;
        if (!new_owner) new_owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_group_t *g = gid ? miku_group_find(svc, gid) : NULL;
        if (!g || !new_owner || !new_owner[0]) {
            miku_ji(resp, "errCode", 3001);
            break;
        }
        if (!op || !op[0] || strcmp(op, g->owner_user_id) != 0) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        int new_mi = member_index_find(svc, gid, new_owner);
        if (new_mi < 0) {
            miku_ji(resp, "errCode", 3002);
            break;
        }
        int old_mi = member_index_find(svc, gid, g->owner_user_id);
        if (old_mi >= 0) svc->members[old_mi].role_level = 20;
        svc->members[new_mi].role_level = 100;
        strncpy(g->owner_user_id, new_owner, sizeof(g->owner_user_id) - 1);
        g->owner_user_id[sizeof(g->owner_user_id) - 1] = '\0';
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_getJoinedGroupList:
    {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid) uid = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (uid) {
            for (int i = 0; i < svc->member_count; i++) {
                if (strcmp(svc->members[i].user_id, uid) != 0) continue;
                miku_group_t *g = miku_group_find(svc, svc->members[i].group_id);
                if (g && g->status == 0) miku_json_array_push(arr, miku_group_to_json(g));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getGroupApplicationList:
    case MK_GROUP_RPC_getGroupApplicantList:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } break;
    case MK_GROUP_RPC_acceptGroupApplication:
    case MK_GROUP_RPC_refuseGroupApplication:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_getGroupMemberUserID:
    case MK_GROUP_RPC_getFullGroupMemberUserIDs:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (gid) {
            int gi = group_index_find(svc, gid);
            if (gi >= 0) {
                for (int mi = svc->member_head[gi]; mi >= 0; mi = svc->member_next[mi])
                    miku_json_array_push(arr, miku_json_create_str(svc->members[mi].user_id));
            } else {
                for (int i = 0; i < svc->member_count; i++)
                    if (strcmp(svc->members[i].group_id, gid) == 0)
                        miku_json_array_push(arr, miku_json_create_str(svc->members[i].user_id));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_muteGroupMember:
    case MK_GROUP_RPC_cancelMuteGroupMember:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_getFullJoinGroupIDs:
    {
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        if (!uid) uid = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        if (uid) {
            for (int i = 0; i < svc->member_count; i++) {
                if (strcmp(svc->members[i].user_id, uid) != 0) continue;
                miku_json_array_push(arr,
                    miku_json_create_str(svc->members[i].group_id));
            }
        }
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getGroupAbstractInfo:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getGroupApplicationUnhandledCount:
    {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "count", 0);
    } break;
    case MK_GROUP_RPC_getGroupUsersReqApplicationList:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getGroups:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getIncrementalGroupMemberBatch:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getIncrementalGroupMembers:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getIncrementalJoinGroups:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getRecvGroupApplicationList:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_getSpecifiedUserGroupRequestInfo:
    {
        miku_ji(resp, "errCode", 0);
    } break;
    case MK_GROUP_RPC_getUserReqGroupApplicationList:
    {
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        miku_json_object_set(resp, "data", arr);
    } break;
    case MK_GROUP_RPC_setGroupInfoEx:
    {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *op = req ? miku_json_str(miku_json_get(req, "opUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "fromUserID")) : NULL;
        if (!op || !op[0]) op = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        miku_group_t *g = gid ? miku_group_find(svc, gid) : NULL;
        if (!g) {
            miku_ji(resp, "errCode", 3001);
            break;
        }
        if (!op || !op[0] || miku_group_member_role(svc, gid, op) < 60) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        const char *name = req ? miku_json_str(miku_json_get(req, "groupName")) : NULL;
        const char *ex = req ? miku_json_str(miku_json_get(req, "ex")) : NULL;
        if (name) strncpy(g->group_name, name, sizeof(g->group_name) - 1);
        if (ex) strncpy(g->ex, ex, sizeof(g->ex) - 1);
        miku_ji(resp, "errCode", 0);
    } break;
    default:
        miku_ji(resp, "errCode", 404);
        break;
    }
}

