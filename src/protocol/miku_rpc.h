#ifndef MIKU_RPC_H
#define MIKU_RPC_H

#include "miku_common.h"

#define MK_RPC_MAGIC     0x4D4B
#define MK_RPC_VERSION   1
#define MK_RPC_HDR_SIZE  16

typedef enum {
    MK_RPC_CALL     = 1,
    MK_RPC_REPLY    = 2,
    MK_RPC_PUSH     = 3,
    MK_RPC_KICK     = 4
} miku_rpc_type_t;

typedef struct {
    uint16_t         magic;
    uint8_t          version;
    uint8_t          msg_type;
    uint32_t         seq;
    uint32_t         service;
    uint32_t         method;
} miku_rpc_header_t;

typedef struct {
    miku_rpc_header_t header;
    uint8_t          *payload;
    size_t            payload_len;
} miku_rpc_message_t;

MIKU_API void miku_rpc_header_init(miku_rpc_header_t *hdr, miku_rpc_type_t type,
                                    uint32_t seq, uint32_t service, uint32_t method);

MIKU_API int  miku_rpc_header_encode(const miku_rpc_header_t *hdr, uint8_t out[16]);
MIKU_API int  miku_rpc_header_decode(miku_rpc_header_t *hdr, const uint8_t data[16]);

MIKU_API miku_rpc_message_t *miku_rpc_message_create(miku_rpc_type_t type, uint32_t seq,
                                                       uint32_t service, uint32_t method);
MIKU_API void miku_rpc_message_destroy(miku_rpc_message_t *msg);

MIKU_API int  miku_rpc_message_encode(const miku_rpc_message_t *msg, uint8_t **out, size_t *out_len);
MIKU_API miku_rpc_message_t *miku_rpc_message_decode(const uint8_t *data, size_t len);

MIKU_API int  miku_rpc_message_set_payload(miku_rpc_message_t *msg, const uint8_t *data, size_t len);

#endif
