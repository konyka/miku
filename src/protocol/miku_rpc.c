#include "miku_rpc.h"
#include <stdlib.h>
#include <string.h>

static void write_u16_be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static void write_u32_be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v & 0xFF);
}

static uint16_t read_u16_be(const uint8_t *p) {
    return (uint16_t)((p[0] << 8) | p[1]);
}

static uint32_t read_u32_be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

void miku_rpc_header_init(miku_rpc_header_t *hdr, miku_rpc_type_t type,
                           uint32_t seq, uint32_t service, uint32_t method) {
    if (!hdr) return;
    hdr->magic = MK_RPC_MAGIC;
    hdr->version = MK_RPC_VERSION;
    hdr->msg_type = (uint8_t)type;
    hdr->seq = seq;
    hdr->service = service;
    hdr->method = method;
}

int miku_rpc_header_encode(const miku_rpc_header_t *hdr, uint8_t out[16]) {
    if (!hdr || !out) return -1;
    write_u16_be(out + 0, hdr->magic);
    out[2] = hdr->version;
    out[3] = hdr->msg_type;
    write_u32_be(out + 4, hdr->seq);
    write_u32_be(out + 8, hdr->service);
    write_u32_be(out + 12, hdr->method);
    return 16;
}

int miku_rpc_header_decode(miku_rpc_header_t *hdr, const uint8_t data[16]) {
    if (!hdr || !data) return -1;
    hdr->magic = read_u16_be(data + 0);
    hdr->version = data[2];
    hdr->msg_type = data[3];
    hdr->seq = read_u32_be(data + 4);
    hdr->service = read_u32_be(data + 8);
    hdr->method = read_u32_be(data + 12);
    if (hdr->magic != MK_RPC_MAGIC) return -1;
    return 16;
}

miku_rpc_message_t *miku_rpc_message_create(miku_rpc_type_t type, uint32_t seq,
                                              uint32_t service, uint32_t method) {
    miku_rpc_message_t *msg = (miku_rpc_message_t *)calloc(1, sizeof(*msg));
    if (!msg) return NULL;
    miku_rpc_header_init(&msg->header, type, seq, service, method);
    return msg;
}

void miku_rpc_message_destroy(miku_rpc_message_t *msg) {
    if (!msg) return;
    free(msg->payload);
    free(msg);
}

int miku_rpc_message_encode(const miku_rpc_message_t *msg, uint8_t **out, size_t *out_len) {
    if (!msg || !out || !out_len) return -1;
    uint32_t payload_len_u32 = (uint32_t)msg->payload_len;
    size_t total = MK_RPC_HDR_SIZE + 4 + msg->payload_len;
    uint8_t *buf = (uint8_t *)malloc(total);
    if (!buf) return -1;

    miku_rpc_header_encode(&msg->header, buf);
    write_u32_be(buf + MK_RPC_HDR_SIZE, payload_len_u32);
    if (msg->payload_len > 0 && msg->payload)
        memcpy(buf + MK_RPC_HDR_SIZE + 4, msg->payload, msg->payload_len);

    *out = buf;
    *out_len = total;
    return 0;
}

miku_rpc_message_t *miku_rpc_message_decode(const uint8_t *data, size_t len) {
    if (!data || len < (size_t)(MK_RPC_HDR_SIZE + 4)) return NULL;

    miku_rpc_message_t *msg = (miku_rpc_message_t *)calloc(1, sizeof(*msg));
    if (!msg) return NULL;

    if (miku_rpc_header_decode(&msg->header, data) != 16) {
        free(msg);
        return NULL;
    }

    uint32_t payload_len = read_u32_be(data + MK_RPC_HDR_SIZE);
    if (MK_RPC_HDR_SIZE + 4 + payload_len > len) {
        free(msg);
        return NULL;
    }

    msg->payload_len = payload_len;
    if (payload_len > 0) {
        msg->payload = (uint8_t *)malloc(payload_len);
        if (!msg->payload) { free(msg); return NULL; }
        memcpy(msg->payload, data + MK_RPC_HDR_SIZE + 4, payload_len);
    }
    return msg;
}

int miku_rpc_message_set_payload(miku_rpc_message_t *msg, const uint8_t *data, size_t len) {
    if (!msg) return -1;
    free(msg->payload);
    msg->payload = NULL;
    msg->payload_len = 0;
    if (len > 0 && data) {
        msg->payload = (uint8_t *)malloc(len);
        if (!msg->payload) return -1;
        memcpy(msg->payload, data, len);
        msg->payload_len = len;
    }
    return 0;
}
