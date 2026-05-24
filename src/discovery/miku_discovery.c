#include "miku_discovery.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

struct miku_discovery_s {
    char                  endpoints[512];
    miku_service_entry_t *entries;
    int                   entry_count;
    int                   entry_cap;
};

miku_discovery_t *miku_discovery_create(const char *endpoints) {
    if (!endpoints) return NULL;
    miku_discovery_t *d = (miku_discovery_t *)calloc(1, sizeof(*d));
    if (!d) return NULL;
    strncpy(d->endpoints, endpoints, sizeof(d->endpoints) - 1);
    d->entry_cap = 32;
    d->entries = (miku_service_entry_t *)calloc((size_t)d->entry_cap, sizeof(miku_service_entry_t));
    if (!d->entries) { free(d); return NULL; }
    return d;
}

void miku_discovery_destroy(miku_discovery_t *d) {
    if (!d) return;
    free(d->entries);
    free(d);
}

int miku_discovery_register(miku_discovery_t *d, const char *service_name,
                             const char *host, int port, int64_t ttl_sec) {
    if (!d || !service_name || !host) return -1;
    for (int i = 0; i < d->entry_count; i++) {
        if (strcmp(d->entries[i].name, service_name) == 0) {
            strncpy(d->entries[i].host, host, sizeof(d->entries[i].host) - 1);
            d->entries[i].port = port;
            d->entries[i].lease_id = ttl_sec;
            MK_LOG_INFO("Service updated: %s -> %s:%d", service_name, host, port);
            return 0;
        }
    }
    if (d->entry_count >= d->entry_cap) {
        d->entry_cap *= 2;
        d->entries = (miku_service_entry_t *)realloc(d->entries,
            (size_t)d->entry_cap * sizeof(miku_service_entry_t));
        if (!d->entries) return -1;
    }
    miku_service_entry_t *e = &d->entries[d->entry_count++];
    strncpy(e->name, service_name, sizeof(e->name) - 1);
    strncpy(e->host, host, sizeof(e->host) - 1);
    e->port = port;
    e->lease_id = ttl_sec;
    MK_LOG_INFO("Service registered: %s -> %s:%d", service_name, host, port);
    return 0;
}

int miku_discovery_deregister(miku_discovery_t *d, const char *service_name) {
    if (!d || !service_name) return -1;
    for (int i = 0; i < d->entry_count; i++) {
        if (strcmp(d->entries[i].name, service_name) == 0) {
            memmove(&d->entries[i], &d->entries[i + 1],
                (size_t)(d->entry_count - i - 1) * sizeof(miku_service_entry_t));
            d->entry_count--;
            MK_LOG_INFO("Service deregistered: %s", service_name);
            return 0;
        }
    }
    return -1;
}

int miku_discovery_resolve(miku_discovery_t *d, const char *service_name,
                            miku_service_entry_t *entries, int max_entries) {
    if (!d || !service_name || !entries) return 0;
    int count = 0;
    for (int i = 0; i < d->entry_count && count < max_entries; i++) {
        if (strcmp(d->entries[i].name, service_name) == 0) {
            entries[count++] = d->entries[i];
        }
    }
    return count;
}

int miku_discovery_heartbeat(miku_discovery_t *d) {
    if (!d) return -1;
    return 0;
}
