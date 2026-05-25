#include "miku_third.h"
#include "miku_json_util.h"
#include <stdlib.h>
#include <string.h>

struct miku_third_service_s { int unused; };

miku_third_service_t *miku_third_service_create(void) {
    return (miku_third_service_t *)calloc(1, sizeof(miku_third_service_t));
}
void miku_third_service_destroy(miku_third_service_t *svc) { free(svc); }

void miku_third_handle_rpc(miku_third_service_t *svc, const char *method,
                            const miku_json_val_t *req, miku_json_val_t *resp) {
    (void)svc; (void)req;
    if (!method || !resp) return;
    if (strcmp(method, "getUploadToken") == 0) {
       miku_ji(resp, "errCode", 0);
       miku_jss(resp, "token", "placeholder_upload_token");
    } else if (strcmp(method, "getDownloadURL") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "url", "https://placeholder.example.com/file");
    } else if (strcmp(method, "accessURL") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "url", "https://placeholder.example.com/access");
    } else if (strcmp(method, "deleteObject") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "initiateMultipartUpload") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadID", "multipart_placeholder");
    } else if (strcmp(method, "completeMultipartUpload") == 0) {
        miku_ji(resp, "errCode", 0);
    } else if (strcmp(method, "getUploadInfo") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_jss(resp, "uploadURL", "https://placeholder.example.com/upload");
    } else if (strcmp(method, "getObjectInfo") == 0) {
        miku_ji(resp, "errCode", 0);
        miku_ji(resp, "size", 0);
    } else if (strcmp(method, "getSignalInvitationInfo") == 0) {
        miku_ji(resp, "errCode", 0);
    } else {
        miku_ji(resp, "errCode", 404);
    }
}
