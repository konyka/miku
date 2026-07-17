#ifndef MIKU_RPC_CLIENT_H
#define MIKU_RPC_CLIENT_H

#include "miku_common.h"
#include <stddef.h>

/* Copy payload JSON with internalToken field added (for split-deploy RPC). */
MIKU_API int miku_rpc_json_add_internal_token(const char *payload_json,
                                               char *out, size_t out_cap);

/* TCP RPC to host:port. payload_json is the request object JSON.
 * with_internal_token: inject internalToken before send.
 * Returns 0 on success and fills resp_body with response JSON. */
MIKU_API int miku_rpc_call(const char *host, int port, const char *payload_json,
                            char *resp_body, size_t resp_cap,
                            int with_internal_token);

#endif
