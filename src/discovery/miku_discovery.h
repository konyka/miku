#ifndef MIKU_DISCOVERY_H
#define MIKU_DISCOVERY_H

#include "miku_common.h"

typedef struct miku_service_entry_s {
    char    name[64];
    char    host[128];
    int     port;
    int64_t lease_id;
} miku_service_entry_t;

typedef struct miku_discovery_s miku_discovery_t;

MIKU_API miku_discovery_t *miku_discovery_create(const char *endpoints);
MIKU_API void              miku_discovery_destroy(miku_discovery_t *d);

MIKU_API int   miku_discovery_register(miku_discovery_t *d, const char *service_name,
                                        const char *host, int port, int64_t ttl_sec);
MIKU_API int   miku_discovery_deregister(miku_discovery_t *d, const char *service_name);
MIKU_API int   miku_discovery_resolve(miku_discovery_t *d, const char *service_name,
                                       miku_service_entry_t *entries, int max_entries);
MIKU_API int   miku_discovery_heartbeat(miku_discovery_t *d);

#endif
