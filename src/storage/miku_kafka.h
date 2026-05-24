#ifndef MIKU_KAFKA_H
#define MIKU_KAFKA_H

#include "miku_common.h"

typedef struct miku_kafka_producer_s miku_kafka_producer_t;
typedef struct miku_kafka_consumer_s miku_kafka_consumer_t;

typedef void (*miku_kafka_msg_fn)(const char *topic, const uint8_t *key, size_t key_len,
                                   const uint8_t *val, size_t val_len, void *ctx);

MIKU_API miku_kafka_producer_t *miku_kafka_producer_create(const char *brokers);
MIKU_API void  miku_kafka_producer_destroy(miku_kafka_producer_t *p);
MIKU_API int   miku_kafka_producer_send(miku_kafka_producer_t *p, const char *topic,
                                         const char *key, const uint8_t *val, size_t val_len);

MIKU_API miku_kafka_consumer_t *miku_kafka_consumer_create(const char *brokers, const char *group_id);
MIKU_API void  miku_kafka_consumer_destroy(miku_kafka_consumer_t *c);
MIKU_API int   miku_kafka_consumer_subscribe(miku_kafka_consumer_t *c, const char **topics, int count);
MIKU_API int   miku_kafka_consumer_poll(miku_kafka_consumer_t *c, int timeout_ms,
                                         miku_kafka_msg_fn cb, void *ctx);
MIKU_API void  miku_kafka_consumer_stop(miku_kafka_consumer_t *c);

#endif
