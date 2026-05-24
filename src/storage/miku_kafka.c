#include "miku_kafka.h"
#include "miku_log.h"
#include <stdlib.h>
#include <string.h>

#ifdef MIKU_HAS_KAFKA
#include <librdkafka/rdkafka.h>

struct miku_kafka_producer_s {
    rd_kafka_t  *rk;
    char        *brokers;
};

struct miku_kafka_consumer_s {
    rd_kafka_t  *rk;
    rd_kafka_topic_partition_list_t *topics;
    char        *brokers;
    char        *group_id;
    bool         running;
};

static void dr_msg_cb(rd_kafka_t *rk, const rd_kafka_message_t *msg, void *opaque) {
    (void)rk; (void)opaque;
    if (msg->err)
        MK_LOG_WARN("Kafka produce failed: %s", rd_kafka_message_errstr(msg));
}

miku_kafka_producer_t *miku_kafka_producer_create(const char *brokers) {
    if (!brokers) return NULL;
    miku_kafka_producer_t *p = (miku_kafka_producer_t *)calloc(1, sizeof(*p));
    if (!p) return NULL;
    p->brokers = strdup(brokers);

    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    rd_kafka_conf_set_dr_msg_cb(conf, dr_msg_cb);
    char errstr[512];
    rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr));
    p->rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!p->rk) {
        MK_LOG_ERROR("Failed to create Kafka producer: %s", errstr);
        free(p->brokers);
        free(p);
        return NULL;
    }
    return p;
}

void miku_kafka_producer_destroy(miku_kafka_producer_t *p) {
    if (!p) return;
    if (p->rk) {
        rd_kafka_flush(p->rk, 5000);
        rd_kafka_destroy(p->rk);
    }
    free(p->brokers);
    free(p);
}

int miku_kafka_producer_send(miku_kafka_producer_t *p, const char *topic,
                              const char *key, const uint8_t *val, size_t val_len) {
    if (!p || !p->rk || !topic || !val) return -1;
    rd_kafka_topic_t *rkt = rd_kafka_topic_new(p->rk, topic, NULL);
    if (!rkt) return -1;
    int rc = rd_kafka_produce(rkt, RD_KAFKA_PARTITION_UA,
        RD_KAFKA_MSG_F_COPY,
        (void *)val, val_len,
        key, key ? strlen(key) : 0,
        NULL);
    rd_kafka_topic_destroy(rkt);
    return rc == 0 ? 0 : -1;
}

static void rebalance_cb(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                          rd_kafka_topic_partition_list_t *partitions, void *opaque) {
    (void)opaque;
    switch (err) {
        case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
            rd_kafka_assign(rk, partitions);
            break;
        case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
            rd_kafka_assign(rk, NULL);
            break;
        default:
            rd_kafka_assign(rk, NULL);
            break;
    }
}

miku_kafka_consumer_t *miku_kafka_consumer_create(const char *brokers, const char *group_id) {
    if (!brokers || !group_id) return NULL;
    miku_kafka_consumer_t *c = (miku_kafka_consumer_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->brokers = strdup(brokers);
    c->group_id = strdup(group_id);

    rd_kafka_conf_t *conf = rd_kafka_conf_new();
    rd_kafka_conf_set_rebalance_cb(conf, rebalance_cb);
    char errstr[512];
    rd_kafka_conf_set(conf, "bootstrap.servers", brokers, errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "group.id", group_id, errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "auto.offset.reset", "earliest", errstr, sizeof(errstr));

    c->rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, sizeof(errstr));
    if (!c->rk) {
        MK_LOG_ERROR("Failed to create Kafka consumer: %s", errstr);
        free(c->brokers); free(c->group_id); free(c);
        return NULL;
    }
    c->topics = rd_kafka_topic_partition_list_new(4);
    c->running = false;
    return c;
}

void miku_kafka_consumer_destroy(miku_kafka_consumer_t *c) {
    if (!c) return;
    miku_kafka_consumer_stop(c);
    if (c->topics) rd_kafka_topic_partition_list_destroy(c->topics);
    if (c->rk) rd_kafka_destroy(c->rk);
    free(c->brokers); free(c->group_id); free(c);
}

int miku_kafka_consumer_subscribe(miku_kafka_consumer_t *c, const char **topics, int count) {
    if (!c || !c->rk || !topics) return -1;
    rd_kafka_topic_partition_list_destroy(c->topics);
    c->topics = rd_kafka_topic_partition_list_new((size_t)count);
    for (int i = 0; i < count; i++)
        rd_kafka_topic_partition_list_add(c->topics, topics[i], RD_KAFKA_PARTITION_UA);
    rd_kafka_resp_err_t err = rd_kafka_subscribe(c->rk, c->topics);
    return err == RD_KAFKA_RESP_ERR_NO_ERROR ? 0 : -1;
}

int miku_kafka_consumer_poll(miku_kafka_consumer_t *c, int timeout_ms,
                              miku_kafka_msg_fn cb, void *ctx) {
    if (!c || !c->rk || !cb) return -1;
    c->running = true;
    rd_kafka_message_t *msg = rd_kafka_consumer_poll(c->rk, timeout_ms);
    if (!msg) return 0;
    if (msg->err) {
        rd_kafka_message_destroy(msg);
        return -1;
    }
    cb(msg->rkt ? rd_kafka_topic_name(msg->rkt) : "",
      (const uint8_t *)msg->key, msg->key_len,
      (const uint8_t *)msg->payload, msg->len,
      ctx);
    rd_kafka_message_destroy(msg);
    return 1;
}

void miku_kafka_consumer_stop(miku_kafka_consumer_t *c) {
    if (!c) return;
    c->running = false;
    if (c->rk) {
        rd_kafka_consumer_close(c->rk);
        rd_kafka_assign(c->rk, NULL);
    }
}

#else

struct miku_kafka_producer_s { char *brokers; };
struct miku_kafka_consumer_s { char *brokers; bool running; };

miku_kafka_producer_t *miku_kafka_producer_create(const char *brokers) {
    (void)brokers;
    return NULL;
}
void miku_kafka_producer_destroy(miku_kafka_producer_t *p) { (void)p; }
int miku_kafka_producer_send(miku_kafka_producer_t *p, const char *t, const char *k,
                              const uint8_t *v, size_t l) {
    (void)p; (void)t; (void)k; (void)v; (void)l; return -1;
}

miku_kafka_consumer_t *miku_kafka_consumer_create(const char *b, const char *g) {
    (void)b; (void)g; return NULL;
}
void miku_kafka_consumer_destroy(miku_kafka_consumer_t *c) { (void)c; }
int miku_kafka_consumer_subscribe(miku_kafka_consumer_t *c, const char **t, int n) {
    (void)c; (void)t; (void)n; return -1;
}
int miku_kafka_consumer_poll(miku_kafka_consumer_t *c, int t,
                              miku_kafka_msg_fn cb, void *ctx) {
    (void)c; (void)t; (void)cb; (void)ctx; return -1;
}
void miku_kafka_consumer_stop(miku_kafka_consumer_t *c) { (void)c; }

#endif
