#ifndef MIKU_HTTP_CLIENT_H
#define MIKU_HTTP_CLIENT_H

#include "miku_common.h"
#include <stddef.h>

/* POST JSON to http:// URL only. Returns 0 on 2xx, -1 on error. timeout ~200ms. */
MIKU_API int miku_http_post_json(const char *url, const char *payload);

/* Like miku_http_post_json, but copies response body (after headers) into resp_body. */
MIKU_API int miku_http_post_json_resp(const char *url, const char *payload,
                                      char *resp_body, size_t resp_cap);

/* Split-deploy internal calls: adds X-Internal-Secret header. */
MIKU_API int miku_http_post_json_internal(const char *url, const char *payload);
MIKU_API int miku_http_post_json_internal_resp(const char *url, const char *payload,
                                               char *resp_body, size_t resp_cap);

#endif
