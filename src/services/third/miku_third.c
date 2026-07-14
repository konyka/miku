#include "miku_third.h"
#include "miku_hash.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_third_service_s { int unused; };

miku_third_service_t *miku_third_service_create(void) {
    return (miku_third_service_t *)calloc(1, sizeof(miku_third_service_t));
}
void miku_third_service_destroy(miku_third_service_t *svc) { free(svc); }

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
    (void)svc; (void)req;
    if (!method || !resp) return;
    switch (third_rpc_id(method)) {
    case MK_THIRD_RPC_getUploadToken:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "token", "placeholder_upload_token");
        break;
    case MK_THIRD_RPC_getDownloadURL:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "url", "https://placeholder.example.com/file");
        break;
    case MK_THIRD_RPC_accessURL:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "url", "https://placeholder.example.com/access");
        break;
    case MK_THIRD_RPC_deleteObject:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_initiateMultipartUpload:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadID", "multipart_placeholder");
        break;
    case MK_THIRD_RPC_completeMultipartUpload:
        miku_ji(resp, "errCode", 0);
        break;
    case MK_THIRD_RPC_getUploadInfo:
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadURL", "https://placeholder.example.com/upload");
        break;
    case MK_THIRD_RPC_getObjectInfo:
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
