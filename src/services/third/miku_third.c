#include "miku_third.h"
#include <stdlib.h>
#include <string.h>

struct miku_third_service_s { int unused; };

miku_third_service_t *miku_third_service_create(void) {
    return (miku_third_service_t *)calloc(1, sizeof(miku_third_service_t));
}
void miku_third_service_destroy(miku_third_service_t *svc) { free(svc); }

static void js(miku_json_val_t *o, const char *k, const char *v) { if (v) miku_json_object_set(o, k, miku_json_create_str(v)); }
static void ji(miku_json_val_t *o, const char *k, int64_t v) { miku_json_object_set(o, k, miku_json_create_int(v)); }

void miku_third_handle_rpc(miku_third_service_t *svc, const char *method,
                            const miku_json_val_t *req, miku_json_val_t *resp) {
    (void)svc; (void)req;
    if (!method || !resp) return;
    if (strcmp(method, "getUploadToken") == 0) {
        ji(resp, "errCode", 0);
        js(resp, "token", "placeholder_upload_token");
    } else if (strcmp(method, "getDownloadURL") == 0) {
        ji(resp, "errCode", 0);
        js(resp, "url", "https://placeholder.example.com/file");
    } else {
        ji(resp, "errCode", 404);
    }
}
