#include "miku_third.h"
#include "miku_hash.h"
#include "miku_json_util.h"
#include "miku_uuid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct miku_third_service_s {
    miku_object_store_t *store;
};

miku_third_service_t *miku_third_service_create(void) {
    miku_third_service_t *svc = (miku_third_service_t *)calloc(1, sizeof(*svc));
    if (!svc) return NULL;
    svc->store = miku_object_store_create();
    if (!svc->store) {
        free(svc);
        return NULL;
    }
    return svc;
}

void miku_third_service_destroy(miku_third_service_t *svc) {
    if (!svc) return;
    miku_object_store_destroy(svc->store);
    free(svc);
}

miku_object_store_t *miku_third_object_store(miku_third_service_t *svc) {
    return svc ? svc->store : NULL;
}

static const char *req_object_name(const miku_json_val_t *req) {
    static const char *keys[] = {"name", "key", "objectName", "filePath", NULL};
    if (!req) return NULL;
    for (int i = 0; keys[i]; i++) {
        const char *s = miku_json_str(miku_json_get(req, keys[i]));
        if (s && s[0]) return s;
    }
    return NULL;
}

static const char *req_owner(const miku_json_val_t *req) {
    if (!req) return "";
    const char *s = miku_json_str(miku_json_get(req, "userID"));
    if (s && s[0]) return s;
    s = miku_json_str(miku_json_get(req, "ownerUserID"));
    return s ? s : "";
}

enum {
    MK_THIRD_RPC_getUploadToken = 0,
    MK_THIRD_RPC_getDownloadURL = 1,
    MK_THIRD_RPC_accessURL = 2,
    MK_THIRD_RPC_deleteObject = 3,
    MK_THIRD_RPC_initiateMultipartUpload = 4,
    MK_THIRD_RPC_completeMultipartUpload = 5,
    MK_THIRD_RPC_getUploadInfo = 6,
    MK_THIRD_RPC_getObjectInfo = 7,
    MK_THIRD_RPC_getSignalInvitationInfo = 8,
    MK_THIRD_RPC_authSign = 9,
    MK_THIRD_RPC_completeFormData = 10,
    MK_THIRD_RPC_deleteLogs = 11,
    MK_THIRD_RPC_fcmUpdateToken = 12,
    MK_THIRD_RPC_getPrometheus = 13,
    MK_THIRD_RPC_initiateFormData = 14,
    MK_THIRD_RPC_partLimit = 15,
    MK_THIRD_RPC_partSize = 16,
    MK_THIRD_RPC_searchLogs = 17,
    MK_THIRD_RPC_setAppBadge = 18,
    MK_THIRD_RPC_uploadLogs = 19,
    MK_THIRD_RPC_COUNT = 20
};

#define MK_THIRD_RPC_HASH 64
static const char *const g_third_rpc_names[MK_THIRD_RPC_COUNT] = {
    "getUploadToken",
    "getDownloadURL",
    "accessURL",
    "deleteObject",
    "initiateMultipartUpload",
    "completeMultipartUpload",
    "getUploadInfo",
    "getObjectInfo",
    "getSignalInvitationInfo",
    "authSign",
    "completeFormData",
    "deleteLogs",
    "fcmUpdateToken",
    "getPrometheus",
    "initiateFormData",
    "partLimit",
    "partSize",
    "searchLogs",
    "setAppBadge",
    "uploadLogs"
};

static int16_t g_third_rpc_hash[MK_THIRD_RPC_HASH];
static int g_third_rpc_ready;

static void third_rpc_init(void) {
    if (g_third_rpc_ready) return;
    for (int i = 0; i < MK_THIRD_RPC_HASH; i++) g_third_rpc_hash[i] = -1;
    for (int i = 0; i < MK_THIRD_RPC_COUNT; i++) {
        const char *m = g_third_rpc_names[i];
        uint32_t idx = (uint32_t)(miku_fnv1a_64(m, strlen(m)) & (MK_THIRD_RPC_HASH - 1));
        for (int n = 0; n < MK_THIRD_RPC_HASH; n++) {
            if (g_third_rpc_hash[idx] < 0) { g_third_rpc_hash[idx] = (int16_t)i; break; }
            idx = (idx + 1) & (MK_THIRD_RPC_HASH - 1);
        }
    }
    g_third_rpc_ready = 1;
}

static int third_rpc_id(const char *method) {
    if (!method) return -1;
    third_rpc_init();
    uint32_t idx = (uint32_t)(miku_fnv1a_64(method, strlen(method)) & (MK_THIRD_RPC_HASH - 1));
    for (int n = 0; n < MK_THIRD_RPC_HASH; n++) {
        int id = g_third_rpc_hash[idx];
        if (id < 0) return -1;
        if (strcmp(g_third_rpc_names[id], method) == 0) return id;
        idx = (idx + 1) & (MK_THIRD_RPC_HASH - 1);
    }
    return -1;
}

void miku_third_handle_rpc(miku_third_service_t *svc, const char *method,
                            const miku_json_val_t *req, miku_json_val_t *resp) {
    if (!method || !resp) return;
    miku_object_store_t *store = svc ? svc->store : NULL;
    const char *name = req_object_name(req);
    switch (third_rpc_id(method)) {
    case MK_THIRD_RPC_getUploadToken:
        miku_ji(resp, "errCode", 0);
        if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            miku_jss(resp, "errMsg", "object path required");
            break;
        }
        miku_jss(resp, "token", name && name[0] ? name : "placeholder_upload_token");
        break;
    case MK_THIRD_RPC_getDownloadURL:
    case MK_THIRD_RPC_accessURL: {
        if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        if (name && store) {
            miku_object_meta_t meta;
            if (miku_object_store_get(store, name, &meta) != 0) {
                miku_ji(resp, "errCode", 1004);
                miku_jss(resp, "errMsg", "object not found");
                break;
            }
            char url[320];
            snprintf(url, sizeof(url), "miku-obj://%s", name);
            miku_ji(resp, "errCode", 0);
            miku_jss(resp, "url", url);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "url", method[0] == 'a'
                 ? "https://placeholder.example.com/access"
                 : "https://placeholder.example.com/file");
        break;
    }
    case MK_THIRD_RPC_deleteObject:
        if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        if (name && store) miku_object_store_delete(store, name);
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_initiateMultipartUpload: {
        char upload_id[40];
        if (name && store && miku_object_name_is_safe(name)) {
            miku_object_meta_t in;
            memset(&in, 0, sizeof(in));
            strncpy(in.name, name, sizeof(in.name) - 1);
            strncpy(in.owner, req_owner(req), sizeof(in.owner) - 1);
            miku_uuid_generate(in.upload_id);
            in.created_ms = miku_timestamp_ms();
            in.expire_ms = in.created_ms + 86400000LL;
            in.complete = 0;
            if (miku_object_store_upsert(store, &in) == 0)
                strncpy(upload_id, in.upload_id, sizeof(upload_id) - 1);
            else
                strncpy(upload_id, "multipart_placeholder", sizeof(upload_id) - 1);
        } else if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            break;
        } else {
            strncpy(upload_id, "multipart_placeholder", sizeof(upload_id) - 1);
        }
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadID", upload_id);
        break;
    }
    case MK_THIRD_RPC_completeMultipartUpload: {
        if (name && store && miku_object_name_is_safe(name)) {
            miku_object_meta_t meta;
            if (miku_object_store_get(store, name, &meta) == 0) {
                int64_t sz = miku_json_int(miku_json_get(req, "size"));
                if (sz > 0) meta.size = sz;
                meta.complete = 1;
                miku_object_store_upsert(store, &meta);
            }
        } else if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        miku_ji(resp, "errCode", 0);
        break;
    }
    case MK_THIRD_RPC_getUploadInfo:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadURL", "https://placeholder.example.com/upload");
        break;
    case MK_THIRD_RPC_getObjectInfo:
        if (name && !miku_object_name_is_safe(name)) {
            miku_ji(resp, "errCode", 3003);
            break;
        }
        if (name && store) {
            miku_object_meta_t meta;
            if (miku_object_store_get(store, name, &meta) != 0) {
                miku_ji(resp, "errCode", 1004);
                miku_jss(resp, "errMsg", "object not found");
                break;
            }
            miku_ji(resp, "errCode", 0);
            miku_ji(resp, "size", meta.size);
            miku_jss(resp, "name", meta.name);
            break;
        }
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "size", 0);
        break;
    case MK_THIRD_RPC_getSignalInvitationInfo:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_authSign:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_completeFormData:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_deleteLogs:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_fcmUpdateToken:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_getPrometheus:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_initiateFormData:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadURL", "https://placeholder.example.com/form-data");
        break;
    case MK_THIRD_RPC_partLimit:
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "limit", 5);
        break;
    case MK_THIRD_RPC_partSize:
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "size", 4194304);
        break;
    case MK_THIRD_RPC_searchLogs:
        miku_ji(resp, "errCode", 0);
        miku_json_object_set(resp, "data", miku_json_create_array());
        break;
    case MK_THIRD_RPC_setAppBadge:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_uploadLogs:
        miku_ji(resp, "errCode", 0);
        break;
    default:
        miku_ji(resp, "errCode", 404);
        break;
    }
}
