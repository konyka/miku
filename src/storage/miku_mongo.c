#include "miku_mongo.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#ifdef MIKU_HAS_MONGO
#include <mongoc/mongoc.h>

struct miku_mongo_s {
    mongoc_client_t      *client;
    mongoc_database_t    *database;
    char                 *db_name;
    char                 *uri_str;
    bool                  connected;
};

miku_mongo_t *miku_mongo_create(const char *uri, const char *db) {
    if (!uri || !db) return NULL;
    miku_mongo_t *m = (miku_mongo_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->uri_str = strdup(uri);
    m->db_name = strdup(db);
    return m;
}

void miku_mongo_destroy(miku_mongo_t *m) {
    if (!m) return;
    miku_mongo_disconnect(m);
    free(m->uri_str);
    free(m->db_name);
    free(m);
}

int miku_mongo_connect(miku_mongo_t *m) {
    if (!m) return -1;
    if (m->connected) return 0;

    mongoc_init();
    mongoc_uri_t *uri = mongoc_uri_new(m->uri_str);
    if (!uri) {
        MK_LOG_ERROR("Invalid MongoDB URI: %s", m->uri_str);
        return -1;
    }
    m->client = mongoc_client_new_from_uri(uri);
    mongoc_uri_destroy(uri);
    if (!m->client) {
        MK_LOG_ERROR("Failed to create MongoDB client");
        return -1;
    }

    bson_error_t err;
    if (!mongoc_client_command_simple(m->client, "admin",
            BCON_NEW("ping", BCON_INT32(1)), NULL, NULL, &err)) {
        MK_LOG_ERROR("MongoDB ping failed: %s", err.message);
        mongoc_client_destroy(m->client);
        m->client = NULL;
        return -1;
    }

    m->database = mongoc_client_get_database(m->client, m->db_name);
    m->connected = true;
    MK_LOG_INFO("MongoDB connected: %s (db=%s)", m->uri_str, m->db_name);
    return 0;
}

void miku_mongo_disconnect(miku_mongo_t *m) {
    if (!m || !m->connected) return;
    if (m->database) mongoc_database_destroy(m->database);
    if (m->client) mongoc_client_destroy(m->client);
    m->database = NULL;
    m->client = NULL;
    m->connected = false;
    MK_LOG_INFO("MongoDB disconnected");
}

bool miku_mongo_is_connected(const miku_mongo_t *m) {
    return m ? m->connected : false;
}

int miku_mongo_insert(miku_mongo_t *m, const char *collection, const char *json_doc) {
    if (!m || !m->connected || !collection || !json_doc) return -1;
    mongoc_collection_t *col = mongoc_client_get_collection(m->client, m->db_name, collection);
    bson_error_t err;
    bson_t *doc = bson_new_from_json((const uint8_t *)json_doc, -1, &err);
    if (!doc) {
        MK_LOG_ERROR("Invalid JSON for insert: %s", err.message);
        mongoc_collection_destroy(col);
        return -1;
    }
    bool ok = mongoc_collection_insert_one(col, doc, NULL, NULL, &err);
    bson_destroy(doc);
    mongoc_collection_destroy(col);
    if (!ok) {
        MK_LOG_ERROR("MongoDB insert failed: %s", err.message);
        return -1;
    }
    return 0;
}

int miku_mongo_find_one(miku_mongo_t *m, const char *collection,
                         const char *json_filter, char **result) {
    if (!m || !m->connected || !collection || !json_filter || !result) return -1;
    mongoc_collection_t *col = mongoc_client_get_collection(m->client, m->db_name, collection);
    bson_error_t err;
    bson_t *filter = bson_new_from_json((const uint8_t *)json_filter, -1, &err);
    if (!filter) {
        mongoc_collection_destroy(col);
        return -1;
    }
    mongoc_cursor_t *cursor = mongoc_collection_find_with_opts(col, filter, NULL, NULL);
    const bson_t *doc;
    int rc = -1;
    if (mongoc_cursor_next(cursor, &doc)) {
        char *json = bson_as_canonical_extended_json(doc, NULL);
        *result = strdup(json);
        bson_free(json);
        rc = 0;
    }
    mongoc_cursor_destroy(cursor);
    bson_destroy(filter);
    mongoc_collection_destroy(col);
    return rc;
}

int miku_mongo_update(miku_mongo_t *m, const char *collection,
                       const char *json_filter, const char *json_update, bool upsert) {
    if (!m || !m->connected || !collection) return -1;
    mongoc_collection_t *col = mongoc_client_get_collection(m->client, m->db_name, collection);
    bson_error_t err;
    bson_t *filter = bson_new_from_json((const uint8_t *)json_filter, -1, &err);
    bson_t *update = bson_new_from_json((const uint8_t *)json_update, -1, &err);
    if (!filter || !update) {
        bson_destroy(filter);
        bson_destroy(update);
        mongoc_collection_destroy(col);
        return -1;
    }
    bson_t opts = BSON_INITIALIZER;
    if (upsert) BSON_APPEND_BOOL(&opts, "upsert", true);
    bool ok = mongoc_collection_update_one(col, filter, update, &opts, NULL, &err);
    bson_destroy(&opts);
    bson_destroy(filter);
    bson_destroy(update);
    mongoc_collection_destroy(col);
    return ok ? 0 : -1;
}

int miku_mongo_delete(miku_mongo_t *m, const char *collection, const char *json_filter) {
    if (!m || !m->connected || !collection || !json_filter) return -1;
    mongoc_collection_t *col = mongoc_client_get_collection(m->client, m->db_name, collection);
    bson_error_t err;
    bson_t *filter = bson_new_from_json((const uint8_t *)json_filter, -1, &err);
    if (!filter) {
        mongoc_collection_destroy(col);
        return -1;
    }
    bool ok = mongoc_collection_delete_one(col, filter, NULL, NULL, &err);
    bson_destroy(filter);
    mongoc_collection_destroy(col);
    return ok ? 0 : -1;
}

#else

struct miku_mongo_s {
    char *db_name;
    char *uri_str;
    bool connected;
};

miku_mongo_t *miku_mongo_create(const char *uri, const char *db) {
    if (!uri || !db) return NULL;
    miku_mongo_t *m = (miku_mongo_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->uri_str = strdup(uri);
    m->db_name = strdup(db);
    return m;
}

void miku_mongo_destroy(miku_mongo_t *m) {
    if (!m) return;
    free(m->uri_str);
    free(m->db_name);
    free(m);
}

int miku_mongo_connect(miku_mongo_t *m) {
    (void)m;
    return -1;
}

void miku_mongo_disconnect(miku_mongo_t *m) {
    if (m) m->connected = false;
}

bool miku_mongo_is_connected(const miku_mongo_t *m) {
    (void)m;
    return false;
}

int miku_mongo_insert(miku_mongo_t *m, const char *col, const char *doc) {
    (void)m; (void)col; (void)doc;
    return -1;
}

int miku_mongo_find_one(miku_mongo_t *m, const char *col, const char *filt, char **res) {
    (void)m; (void)col; (void)filt; (void)res;
    return -1;
}

int miku_mongo_update(miku_mongo_t *m, const char *col, const char *filt,
                       const char *upd, bool upsert) {
    (void)m; (void)col; (void)filt; (void)upd; (void)upsert;
    return -1;
}

int miku_mongo_delete(miku_mongo_t *m, const char *col, const char *filt) {
    (void)m; (void)col; (void)filt;
    return -1;
}

#endif
