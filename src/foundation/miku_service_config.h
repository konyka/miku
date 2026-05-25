#ifndef MIKU_SERVICE_CONFIG_H
#define MIKU_SERVICE_CONFIG_H

#include "miku_common.h"

typedef struct {
    char listen_ip[64];
    int  api_port;
    int  ws_port;
    int  rpc_auth_port;
    int  rpc_user_port;
    int  rpc_friend_port;
    int  rpc_group_port;
    int  rpc_conversation_port;
    int  rpc_msg_port;
    int  rpc_third_port;
    char mongo_uri[256];
    char mongo_database[64];
    int  mongo_pool_size;
    char redis_address[128];
    int  redis_db;
    int  redis_pool_size;
    char kafka_brokers[256];
    char kafka_topic[64];
    char kafka_group_id[64];
    char log_level[32];
    char log_output[64];
} miku_service_config_t;

int  miku_service_config_load(miku_service_config_t *out, const char *config_dir);
void miku_service_config_print(const miku_service_config_t *cfg);

#endif
