#ifndef MIKU_HTTP_H
#define MIKU_HTTP_H

#include "miku_common.h"
#include "miku_string.h"
#include "miku_hashmap.h"

typedef enum {
    MK_HTTP_GET    = 1,
    MK_HTTP_POST   = 2,
    MK_HTTP_PUT    = 3,
    MK_HTTP_DELETE = 4,
    MK_HTTP_PATCH  = 5,
    MK_HTTP_HEAD   = 6,
    MK_HTTP_OPTIONS = 7
} miku_http_method_t;

typedef struct {
    miku_http_method_t  method;
    miku_str_t          path;
    miku_str_t          query_string;
    miku_str_t          body;
    int                 version;
    miku_hashmap_t     *headers;
} miku_http_request_t;

typedef struct {
    int                 status;
    miku_string_t      *body;
    miku_hashmap_t     *headers;
} miku_http_response_t;

typedef void (*miku_http_handler_fn)(miku_http_request_t *req, miku_http_response_t *resp, void *ctx);

MIKU_API miku_http_request_t  *miku_http_request_create(void);
MIKU_API int                   miku_http_request_parse(miku_http_request_t *req, const char *data, size_t len);
MIKU_API void                  miku_http_request_destroy(miku_http_request_t *req);

MIKU_API miku_http_response_t *miku_http_response_create(void);
MIKU_API void                  miku_http_response_set_json(miku_http_response_t *resp, const char *json);
MIKU_API miku_string_t        *miku_http_response_serialize(const miku_http_response_t *resp);
MIKU_API void                  miku_http_response_destroy(miku_http_response_t *resp);

MIKU_API const char *miku_http_status_text(int status);
MIKU_API const char *miku_http_method_name(miku_http_method_t method);
MIKU_API miku_http_method_t miku_http_method_from_str(const char *s, size_t len);

#endif
