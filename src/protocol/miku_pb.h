#ifndef MIKU_PB_H
#define MIKU_PB_H

#include "miku_common.h"
#include "miku_string.h"

typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} miku_pb_buf_t;

typedef enum {
    MK_PB_VARINT  = 0,
    MK_PB_FIXED64 = 1,
    MK_PB_BYTES   = 2,
    MK_PB_FIXED32 = 5
} miku_pb_wire_t;

MIKU_API miku_pb_buf_t *miku_pb_buf_create(size_t initial_cap);
MIKU_API void           miku_pb_buf_destroy(miku_pb_buf_t *buf);

MIKU_API int  miku_pb_write_varint(miku_pb_buf_t *buf, uint32_t field, uint64_t val);
MIKU_API int  miku_pb_write_svarint(miku_pb_buf_t *buf, uint32_t field, int64_t val);
MIKU_API int  miku_pb_write_fixed32(miku_pb_buf_t *buf, uint32_t field, uint32_t val);
MIKU_API int  miku_pb_write_fixed64(miku_pb_buf_t *buf, uint32_t field, uint64_t val);
MIKU_API int  miku_pb_write_sfixed32(miku_pb_buf_t *buf, uint32_t field, int32_t val);
MIKU_API int  miku_pb_write_sfixed64(miku_pb_buf_t *buf, uint32_t field, int64_t val);
MIKU_API int  miku_pb_write_bool(miku_pb_buf_t *buf, uint32_t field, bool val);
MIKU_API int  miku_pb_write_bytes(miku_pb_buf_t *buf, uint32_t field, const uint8_t *data, size_t len);
MIKU_API int  miku_pb_write_string(miku_pb_buf_t *buf, uint32_t field, const char *str);

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} miku_pb_reader_t;

MIKU_API void miku_pb_reader_init(miku_pb_reader_t *r, const uint8_t *data, size_t len);
MIKU_API bool miku_pb_read_field(miku_pb_reader_t *r, uint32_t *field, miku_pb_wire_t *wire_type);
MIKU_API bool miku_pb_read_varint(miku_pb_reader_t *r, uint64_t *val);
MIKU_API bool miku_pb_read_svarint(miku_pb_reader_t *r, int64_t *val);
MIKU_API bool miku_pb_read_fixed32(miku_pb_reader_t *r, uint32_t *val);
MIKU_API bool miku_pb_read_fixed64(miku_pb_reader_t *r, uint64_t *val);
MIKU_API bool miku_pb_read_bytes(miku_pb_reader_t *r, const uint8_t **data, size_t *len);
MIKU_API bool miku_pb_skip(miku_pb_reader_t *r, miku_pb_wire_t wire_type);

#endif
