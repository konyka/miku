#ifndef MIKU_HTTP_CLIENT_H
#define MIKU_HTTP_CLIENT_H

#include "miku_common.h"

/* POST JSON to http:// URL only. Returns 0 on 2xx, -1 on error. timeout ~200ms. */
MIKU_API int miku_http_post_json(const char *url, const char *payload);

#endif
