#include "miku_common.h"
#include "miku_log.h"
#include "miku_service_config.h"
#include "miku_graceful.h"
#include "miku_msgtransfer.h"
#include "miku_mt_pipeline.h"
#include "miku_msg_store.h"
#include "miku_crontask.h"
#include "miku_cron_tasks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static miku_graceful_t g_graceful;
static miku_msg_store_t *g_store;
static miku_cron_tasks_t *g_cron_impl;

static void pipeline_to_redis(const miku_msg_t *msgs, int count, void *ctx) {
    (void)ctx;
    MK_LOG_INFO("pipeline: flushing %d msgs to Redis", count);
    for (int i = 0; i < count; i++) {
        MK_LOG_DEBUG("  redis: seq=%ld sendID=%s recvID=%s",
                      (long)msgs[i].seq, msgs[i].send_id, msgs[i].recv_id);
    }
}

static void pipeline_to_mongo(const miku_msg_t *msgs, int count, void *ctx) {
    miku_msg_store_t *store = (miku_msg_store_t *)ctx;
    if (!store || !msgs || count <= 0) return;
    int ok = 0;
    for (int i = 0; i < count; i++) {
        const miku_msg_t *m = &msgs[i];
        const char *conv = m->recv_id[0] ? m->recv_id : m->send_id;
        char out_id[64] = {0};
        if (miku_msg_store_insert(store, conv, m->send_id, (int)m->msg_type,
                                   m->content, m->send_time, m->seq,
                                   out_id, sizeof(out_id)) == 0)
            ok++;
    }
    MK_LOG_INFO("pipeline: persisted %d/%d msgs to msg_store (total=%d)",
                ok, count, miku_msg_store_count(store));
}

static void pipeline_to_push(const char *user_id, const char *conv_id, int64_t seq, void *ctx) {
    (void)ctx;
    MK_LOG_INFO("pipeline: push callback user=%s conv=%s seq=%ld",
                 user_id, conv_id, (long)seq);
}

static void cron_delete_msg(void *ctx) {
    miku_cron_delete_expired_msgs((miku_cron_tasks_t *)ctx, 180);
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

    g_store = miku_msg_store_create(NULL);
    g_cron_impl = miku_cron_tasks_create();
    miku_msgtransfer_t *mt = miku_msgtransfer_create();
    if (!mt || !g_store || !g_cron_impl) {
        MK_LOG_ERROR("Failed to create msgtransfer / msg_store / cron");
        return 1;
    }
    miku_cron_tasks_set_msg_store(g_cron_impl, g_store);

    miku_mt_pipeline_t *pipe = miku_mt_pipeline_create();
    if (!pipe) {
        MK_LOG_ERROR("Failed to create pipeline");
        miku_msgtransfer_destroy(mt);
        miku_msg_store_destroy(g_store);
        miku_cron_tasks_destroy(g_cron_impl);
        return 1;
    }

    miku_mt_pipeline_on_redis(pipe, pipeline_to_redis, NULL);
    miku_mt_pipeline_on_mongo(pipe, pipeline_to_mongo, g_store);
    miku_mt_pipeline_on_push(pipe, pipeline_to_push, NULL);

    miku_crontask_t *cron = miku_crontask_create();
    /* In-memory msg_store is process-local: purge must run here, not in miku-crontask. */
    miku_crontask_add(cron, "deleteMsg", cron_delete_msg, g_cron_impl, 86400000);
    miku_crontask_start(cron);

    miku_msgtransfer_start(mt);
    MK_LOG_INFO("miku-msgtransfer ready — msg_store + deleteMsg cron in-process (batch=%d)",
                MK_PIPELINE_BATCH_SIZE);

    int tick = 0;
    while (miku_graceful_running(&g_graceful)) {
        miku_crontask_tick(cron);
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
        usleep(50000);
    }

    MK_LOG_INFO("miku-msgtransfer shutting down (store_count=%d deleted=%ld)",
                miku_msg_store_count(g_store),
                (long)miku_cron_total_msgs_deleted(g_cron_impl));
    miku_mt_pipeline_flush(pipe);
    miku_crontask_stop(cron);
    miku_crontask_destroy(cron);
    miku_msgtransfer_stop(mt);
    miku_msgtransfer_destroy(mt);
    miku_mt_pipeline_destroy(pipe);
    miku_cron_tasks_destroy(g_cron_impl);
    miku_msg_store_destroy(g_store);
    miku_graceful_cleanup(&g_graceful);
    return 0;
}
