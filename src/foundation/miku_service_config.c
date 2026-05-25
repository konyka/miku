#include "miku_service_config.h"
#include "miku_config.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void safe_strcpy(char *dst, const char *src, size_t dstsz) {
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, dstsz - 1);
    dst[dstsz - 1] = '\0';
}

static void try_load(miku_config_t *cfg, const char *dir, const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", dir, filename);
    if (miku_config_load_file(cfg, path) == 0) {
        MK_LOG_INFO("Loaded config: %s", path);
    }
}

int miku_service_config_load(miku_service_config_t *out, const char *config_dir) {
    if (!out || !config_dir) return -1;
    memset(out, 0, sizeof(*out));

    miku_config_t *cfg = miku_config_create();
    if (!cfg) return -1;

    try_load(cfg, config_dir, "share.yml");
    try_load(cfg, config_dir, "mongodb.yml");
    try_load(cfg, config_dir, "redis.yml");
    try_load(cfg, config_dir, "kafka.yml");
    try_load(cfg, config_dir, "log.yml");

    safe_strcpy(out->listen_ip,
                miku_config_get_str(cfg, "listenIP", "0.0.0.0"),
                sizeof(out->listen_ip));
    out->api_port = (int)miku_config_get_int(cfg, "api.port", 10002);
    out->ws_port  = (int)miku_config_get_int(cfg, "msggateway.port", 10001);

    out->rpc_auth_port        = (int)miku_config_get_int(cfg, "rpc.auth.port", 10100);
    out->rpc_user_port        = (int)miku_config_get_int(cfg, "rpc.user.port", 10110);
    out->rpc_friend_port      = (int)miku_config_get_int(cfg, "rpc.friend.port", 10120);
    out->rpc_group_port       = (int)miku_config_get_int(cfg, "rpc.group.port", 10150);
    out->rpc_conversation_port = (int)miku_config_get_int(cfg, "rpc.conversation.port", 10180);
    out->rpc_msg_port         = (int)miku_config_get_int(cfg, "rpc.msg.port", 10130);
    out->rpc_third_port       = (int)miku_config_get_int(cfg, "rpc.third.port", 10200);

    safe_strcpy(out->mongo_uri,
                miku_config_get_str(cfg, "uri", "mongodb://localhost:27017"),
                sizeof(out->mongo_uri));
    safe_strcpy(out->mongo_database,
                miku_config_get_str(cfg, "database", "miku"),
                sizeof(out->mongo_database));
    out->mongo_pool_size = (int)miku_config_get_int(cfg, "maxPoolSize", 64);

    safe_strcpy(out->redis_address,
                miku_config_get_str(cfg, "address", "localhost:6379"),
                sizeof(out->redis_address));
    out->redis_db        = (int)miku_config_get_int(cfg, "db", 0);
    out->redis_pool_size = (int)miku_config_get_int(cfg, "poolSize", 32);

    safe_strcpy(out->kafka_brokers,
                miku_config_get_str(cfg, "brokers", "localhost:9092"),
                sizeof(out->kafka_brokers));
    safe_strcpy(out->kafka_topic,
                miku_config_get_str(cfg, "topic", "miku_msg"),
                sizeof(out->kafka_topic));
    safe_strcpy(out->kafka_group_id,
                miku_config_get_str(cfg, "groupId", "miku-msgtransfer"),
                sizeof(out->kafka_group_id));

    safe_strcpy(out->log_level,
                miku_config_get_str(cfg, "level", "info"),
                sizeof(out->log_level));
    safe_strcpy(out->log_output,
                miku_config_get_str(cfg, "output", "stdout"),
                sizeof(out->log_output));

    miku_config_destroy(cfg);
    return 0;
}

void miku_service_config_print(const miku_service_config_t *cfg) {
    if (!cfg) return;
    MK_LOG_INFO("=== Service Config ===");
    MK_LOG_INFO("  listen_ip:    %s", cfg->listen_ip);
    MK_LOG_INFO("  api_port:     %d", cfg->api_port);
    MK_LOG_INFO("  ws_port:      %d", cfg->ws_port);
    MK_LOG_INFO("  rpc auth:     %d", cfg->rpc_auth_port);
    MK_LOG_INFO("  rpc user:     %d", cfg->rpc_user_port);
    MK_LOG_INFO("  rpc friend:   %d", cfg->rpc_friend_port);
    MK_LOG_INFO("  rpc group:    %d", cfg->rpc_group_port);
    MK_LOG_INFO("  rpc conv:     %d", cfg->rpc_conversation_port);
    MK_LOG_INFO("  rpc msg:      %d", cfg->rpc_msg_port);
    MK_LOG_INFO("  rpc third:    %d", cfg->rpc_third_port);
    MK_LOG_INFO("  mongo:        %s/%s (pool=%d)", cfg->mongo_uri, cfg->mongo_database, cfg->mongo_pool_size);
    MK_LOG_INFO("  redis:        %s db=%d pool=%d", cfg->redis_address, cfg->redis_db, cfg->redis_pool_size);
    MK_LOG_INFO("  kafka:        %s topic=%s group=%s", cfg->kafka_brokers, cfg->kafka_topic, cfg->kafka_group_id);
    MK_LOG_INFO("  log:          %s -> %s", cfg->log_level, cfg->log_output);
}
