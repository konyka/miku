#include "miku_group.h"
#include "miku_uuid.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_group_service_s {
    miku_group_t groups[MK_MAX_GROUPS];
    int group_count;
    miku_group_member_t members[MK_MAX_MEMBERS];
    int member_count;
};

miku_group_service_t *miku_group_service_create(void) {
    return (miku_group_service_t *)calloc(1, sizeof(miku_group_service_t));
}
void miku_group_service_destroy(miku_group_service_t *svc) { free(svc); }

int miku_group_create(miku_group_service_t *svc, miku_group_t *g, const char *owner_uid) {
    if (!svc || !g || svc->group_count >= MK_MAX_GROUPS) return -1;
    miku_uuid_generate(g->group_id);
    strncpy(g->owner_user_id, owner_uid, sizeof(g->owner_user_id) - 1);
    g->create_time = miku_timestamp_ms();
    g->member_count = 1;
    g->status = 0;
    svc->groups[svc->group_count++] = *g;
    miku_group_add_member(svc, g->group_id, owner_uid, 100);
    return 0;
}

miku_group_t *miku_group_find(miku_group_service_t *svc, const char *group_id) {
    if (!svc || !group_id) return NULL;
    for (int i = 0; i < svc->group_count; i++)
        if (strcmp(svc->groups[i].group_id, group_id) == 0) return &svc->groups[i];
    return NULL;
}

int miku_group_add_member(miku_group_service_t *svc, const char *group_id, const char *user_id, int role) {
    if (!svc || !group_id || !user_id || svc->member_count >= MK_MAX_MEMBERS) return -1;
    miku_group_member_t *m = &svc->members[svc->member_count++];
    strncpy(m->group_id, group_id, sizeof(m->group_id) - 1);
    strncpy(m->user_id, user_id, sizeof(m->user_id) - 1);
    m->role_level = role;
    m->join_time = miku_timestamp_ms();
    miku_group_t *g = miku_group_find(svc, group_id);
    if (g) g->member_count++;
    return 0;
}

int miku_group_get_members(miku_group_service_t *svc, const char *group_id, miku_group_member_t *out, int max) {
    if (!svc || !group_id || !out) return 0;
    int n = 0;
    for (int i = 0; i < svc->member_count && n < max; i++)
        if (strcmp(svc->members[i].group_id, group_id) == 0) out[n++] = svc->members[i];
    return n;
}


void miku_group_handle_rpc(miku_group_service_t *svc, const char *method,
                            const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!svc || !method || !resp) return;
    if (strcmp(method, "createGroup") == 0) {
        miku_group_t g;
        memset(&g, 0, sizeof(g));
        const char *name = req ? miku_json_str(miku_json_get(req, "groupName")) : NULL;
        const char *owner = req ? miku_json_str(miku_json_get(req, "ownerUserID")) : NULL;
        if (name) strncpy(g.group_name, name, sizeof(g.group_name) - 1);
        int rc = miku_group_create(svc, &g, owner);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 500);
        if (rc == 0) miku_jss(resp, "data", g.group_id);
    } else if (strcmp(method, "getGroupInfo") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_group_t *g = miku_group_find(svc, gid);
        miku_ji(resp, "errCode", g ? 0 : 3001);
        if (g) miku_json_object_set(resp, "data", miku_group_to_json(g));
    } else if (strcmp(method, "inviteToGroup") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int rc = miku_group_add_member(svc, gid, uid, 20);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } else if (strcmp(method, "getGroupMemberList") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_group_member_t list[256];
        int n = miku_group_get_members(svc, gid, list, 256);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_group_member_to_json(&list[i]));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "getGroupsInfo") == 0) {
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
    } else if (strcmp(method, "setGroupInfo") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_group_t *g = miku_group_find(svc, gid);
        if (g) {
            const char *name = req ? miku_json_str(miku_json_get(req, "groupName")) : NULL;
            if (name) strncpy(g->group_name, name, sizeof(g->group_name) - 1);
        }
        miku_ji(resp, "errCode", g ? 0 : 3001);
    } else if (strcmp(method, "setGroupMemberInfo") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "joinGroup") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        const char *uid = req ? miku_json_str(miku_json_get(req, "userID")) : NULL;
        int rc = miku_group_add_member(svc, gid, uid, 20);
        miku_ji(resp, "errCode", rc == 0 ? 0 : 3002);
    } else if (strcmp(method, "quitGroup") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "dismissGroup") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "muteGroup") == 0 || strcmp(method, "cancelMuteGroup") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "kickGroupMember") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "transferGroupOwner") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getJoinedGroupList") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } else if (strcmp(method, "getGroupApplicationList") == 0 ||
               strcmp(method, "getGroupApplicantList") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
    } else if (strcmp(method, "acceptGroupApplication") == 0 ||
               strcmp(method, "refuseGroupApplication") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getGroupMemberUserID") == 0) {
        const char *gid = req ? miku_json_str(miku_json_get(req, "groupID")) : NULL;
        miku_group_member_t list[256];
        int n = miku_group_get_members(svc, gid, list, 256);
        miku_ji(resp, "errCode", 0);
        miku_json_val_t *arr = miku_json_create_array();
        for (int i = 0; i < n; i++) miku_json_array_push(arr, miku_json_create_str(list[i].user_id));
        miku_json_object_set(resp, "data", arr);
    } else if (strcmp(method, "muteGroupMember") == 0 ||
               strcmp(method, "cancelMuteGroupMember") == 0) {
        miku_ji(resp, "errCode", 0);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
