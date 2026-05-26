#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msgtransfer.h"
#include "miku_mt_pipeline.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static miku_graceful_t g_graceful;

static void pipeline_to_redis(const miku_msg_t *msgs, int count, void *ctx) {
    (void)ctx;
    MK_LOG_INFO("pipeline: flushing %d msgs to Redis", count);
    for (int i = 0; i < count; i++) {
        MK_LOG_DEBUG("  redis: seq=%ld sendID=%s recvID=%s",
                      (long)msgs[i].seq, msgs[i].send_id, msgs[i].recv_id);
    }
}

static void pipeline_to_mongo(const miku_msg_t *msgs, int count, void *ctx) {
    (void)ctx;
    MK_LOG_INFO("pipeline: flushing %d msgs to MongoDB", count);
    for (int i = 0; i < count; i++) {
        MK_LOG_DEBUG("  mongo: msgID=%s clientMsgID=%s",
                      msgs[i].server_msg_id, msgs[i].client_msg_id);
    }
}

static void pipeline_to_push(const char *user_id, const char *conv_id, int64_t seq, void *ctx) {
    (void)ctx;
    MK_LOG_INFO("pipeline: push callback user=%s conv=%s seq=%ld",
                 user_id, conv_id, (long)seq);
}

int main(int argc, char **argv) {
    const char *config_dir = "config/";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) config_dir = argv[++i];
    }

    miku_service_config_t sc;
    miku_service_config_load(&sc, config_dir);

    miku_log_init(NULL, MK_LOG_DEBUG);
    miku_graceful_init(&g_graceful, 300);
    MK_LOG_INFO("miku-msgtransfer starting (kafka: %s/%s)", sc.kafka_brokers, sc.kafka_topic);

    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    if (!mt) { MK_LOG_ERROR("Failed to create msgtransfer"); return 1; }

    miku_mt_pipeline_t *pipe = miku_mt_pipeline_create();
    if (!pipe) { MK_LOG_ERROR("Failed to create pipeline"); miku_msgtransfer_destroy(mt); return 1; }

    miku_mt_pipeline_on_redis(pipe, pipeline_to_redis, NULL);
    miku_mt_pipeline_on_mongo(pipe, pipeline_to_mongo, NULL);
    miku_mt_pipeline_on_push(pipe, pipeline_to_push, NULL);

    miku_msgtransfer_start(mt);
    MK_LOG_INFO("miku-msgtransfer ready — pipeline wired (batch=%d)", MK_PIPELINE_BATCH_SIZE);

    int tick = 0;
    while (miku_graceful_running(&g_graceful)) {
        miku_msg_t msg;
        int drained = 0;
        while (miku_msgtransfer_dequeue(mt, &msg) == 0) {
            msg.seq = miku_mt_pipeline_seq_next(pipe, msg.recv_id);
            miku_mt_pipeline_submit(pipe, &msg);
            drained++;
        }
        if (drained > 0) {
            MK_LOG_DEBUG("msgtransfer: drained %d msgs from queue (pending=%d)",
                          drained, miku_mt_pipeline_pending(pipe));
        }
        tick++;
        if (tick % 10 == 0) {
            miku_mt_pipeline_flush(pipe);
        }
        miku_graceful_wait(&g_graceful, NULL, NULL);
    }

    MK_LOG_INFO("miku-msgtransfer shutting down");
    miku_mt_pipeline_flush(pipe);
    miku_msgtransfer_stop(mt);
    miku_msgtransfer_destroy(mt);
    miku_mt_pipeline_destroy(pipe);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
