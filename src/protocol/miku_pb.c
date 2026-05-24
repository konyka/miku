#include "miku_pb.h"
#include <stdlib.h>
#include <string.h>

miku_pb_buf_t *miku_pb_buf_create(size_t initial_cap) {
    miku_pb_buf_t *buf = (miku_pb_buf_t *)calloc(1, sizeof(*buf));
    if (!buf) return NULL;
    if (initial_cap == 0) initial_cap = 256;
    buf->data = (uint8_t *)malloc(initial_cap);
    if (!buf->data) { free(buf); return NULL; }
    buf->cap = initial_cap;
    buf->len = 0;
    return buf;
}

void miku_pb_buf_destroy(miku_pb_buf_t *buf) {
    if (!buf) return;
    free(buf->data);
    free(buf);
}

static int ensure_cap(miku_pb_buf_t *buf, size_t need) {
    if (buf->len + need <= buf->cap) return 0;
    size_t new_cap = buf->cap;
    while (new_cap < buf->len + need) new_cap *= 2;
    uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
    if (!new_data) return -1;
    buf->data = new_data;
    buf->cap = new_cap;
    return 0;
}

static int encode_tag(miku_pb_buf_t *buf, uint32_t field, miku_pb_wire_t wt) {
    uint64_t tag = ((uint64_t)field << 3) | (uint64_t)wt;
    uint8_t tmp[10];
    int i = 0;
    do {
        tmp[i] = (uint8_t)(tag & 0x7F);
        tag >>= 7;
        if (tag > 0) tmp[i] |= 0x80;
        i++;
    } while (tag > 0);
    if (ensure_cap(buf, (size_t)i) != 0) return -1;
    memcpy(buf->data + buf->len, tmp, (size_t)i);
    buf->len += (size_t)i;
    return 0;
}

static int encode_varint_raw(miku_pb_buf_t *buf, uint64_t val) {
    uint8_t tmp[10];
    int i = 0;
    do {
        tmp[i] = (uint8_t)(val & 0x7F);
        val >>= 7;
        if (val > 0) tmp[i] |= 0x80;
        i++;
    } while (val > 0);
    if (ensure_cap(buf, (size_t)i) != 0) return -1;
    memcpy(buf->data + buf->len, tmp, (size_t)i);
    buf->len += (size_t)i;
    return 0;
}

int miku_pb_write_varint(miku_pb_buf_t *buf, uint32_t field, uint64_t val) {
    if (!buf) return -1;
    if (encode_tag(buf, field, MK_PB_VARINT) != 0) return -1;
    return encode_varint_raw(buf, val);
}

int miku_pb_write_svarint(miku_pb_buf_t *buf, uint32_t field, int64_t val) {
    uint64_t zigzag = (uint64_t)((val << 1) ^ (val >> 63));
    return miku_pb_write_varint(buf, field, zigzag);
}

int miku_pb_write_fixed32(miku_pb_buf_t *buf, uint32_t field, uint32_t val) {
    if (!buf) return -1;
    if (encode_tag(buf, field, MK_PB_FIXED32) != 0) return -1;
    if (ensure_cap(buf, 4) != 0) return -1;
    uint8_t *p = buf->data + buf->len;
    p[0] = (uint8_t)(val);
    p[1] = (uint8_t)(val >> 8);
    p[2] = (uint8_t)(val >> 16);
    p[3] = (uint8_t)(val >> 24);
    buf->len += 4;
    return 0;
}

int miku_pb_write_fixed64(miku_pb_buf_t *buf, uint32_t field, uint64_t val) {
    if (!buf) return -1;
    if (encode_tag(buf, field, MK_PB_FIXED64) != 0) return -1;
    if (ensure_cap(buf, 8) != 0) return -1;
    uint8_t *p = buf->data + buf->len;
    for (int i = 0; i < 8; i++) p[i] = (uint8_t)(val >> (i * 8));
    buf->len += 8;
    return 0;
}

int miku_pb_write_sfixed32(miku_pb_buf_t *buf, uint32_t field, int32_t val) {
    return miku_pb_write_fixed32(buf, field, (uint32_t)val);
}

int miku_pb_write_sfixed64(miku_pb_buf_t *buf, uint32_t field, int64_t val) {
    return miku_pb_write_fixed64(buf, field, (uint64_t)val);
}

int miku_pb_write_bool(miku_pb_buf_t *buf, uint32_t field, bool val) {
    return miku_pb_write_varint(buf, field, val ? 1 : 0);
}

int miku_pb_write_bytes(miku_pb_buf_t *buf, uint32_t field, const uint8_t *data, size_t len) {
    if (!buf) return -1;
    if (encode_tag(buf, field, MK_PB_BYTES) != 0) return -1;
    if (encode_varint_raw(buf, (uint64_t)len) != 0) return -1;
    if (len == 0) return 0;
    if (ensure_cap(buf, len) != 0) return -1;
    memcpy(buf->data + buf->len, data, len);
    buf->len += len;
    return 0;
}

int miku_pb_write_string(miku_pb_buf_t *buf, uint32_t field, const char *str) {
    return miku_pb_write_bytes(buf, field, (const uint8_t *)str, str ? strlen(str) : 0);
}

/* ── Reader ── */

void miku_pb_reader_init(miku_pb_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return;
    r->data = data;
    r->len = len;
    r->pos = 0;
}

bool miku_pb_read_field(miku_pb_reader_t *r, uint32_t *field, miku_pb_wire_t *wire_type) {
    if (!r || r->pos >= r->len) return false;
    uint64_t tag = 0;
    uint32_t shift = 0;
    while (r->pos < r->len) {
        uint8_t b = r->data[r->pos++];
        tag |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    if (field) *field = (uint32_t)(tag >> 3);
    if (wire_type) *wire_type = (miku_pb_wire_t)(tag & 0x07);
    return true;
}

bool miku_pb_read_varint(miku_pb_reader_t *r, uint64_t *val) {
    if (!r || r->pos >= r->len) return false;
    uint64_t v = 0;
    uint32_t shift = 0;
    while (r->pos < r->len) {
        uint8_t b = r->data[r->pos++];
        v |= (uint64_t)(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    if (val) *val = v;
    return true;
}

bool miku_pb_read_svarint(miku_pb_reader_t *r, int64_t *val) {
    uint64_t v;
    if (!miku_pb_read_varint(r, &v)) return false;
    v = (v >> 1) ^ (-(int64_t)(v & 1));
    if (val) *val = (int64_t)v;
    return true;
}

bool miku_pb_read_fixed32(miku_pb_reader_t *r, uint32_t *val) {
    if (!r || r->pos + 4 > r->len) return false;
    uint32_t v = (uint32_t)r->data[r->pos] |
                 ((uint32_t)r->data[r->pos + 1] << 8) |
                 ((uint32_t)r->data[r->pos + 2] << 16) |
                 ((uint32_t)r->data[r->pos + 3] << 24);
    r->pos += 4;
    if (val) *val = v;
    return true;
}

bool miku_pb_read_fixed64(miku_pb_reader_t *r, uint64_t *val) {
    if (!r || r->pos + 8 > r->len) return false;
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)r->data[r->pos + i] << (i * 8);
    r->pos += 8;
    if (val) *val = v;
    return true;
}

bool miku_pb_read_bytes(miku_pb_reader_t *r, const uint8_t **data, size_t *len) {
    uint64_t v;
    if (!miku_pb_read_varint(r, &v)) return false;
    size_t l = (size_t)v;
    if (r->pos + l > r->len) return false;
    if (data) *data = r->data + r->pos;
    if (len) *len = l;
    r->pos += l;
    return true;
}

bool miku_pb_skip(miku_pb_reader_t *r, miku_pb_wire_t wire_type) {
    switch (wire_type) {
        case MK_PB_VARINT: {
            uint64_t v;
            return miku_pb_read_varint(r, &v);
        }
        case MK_PB_FIXED64:
            if (r->pos + 8 > r->len) return false;
            r->pos += 8;
            return true;
        case MK_PB_BYTES: {
            const uint8_t *d;
            size_t l;
            return miku_pb_read_bytes(r, &d, &l);
        }
        case MK_PB_FIXED32:
            if (r->pos + 4 > r->len) return false;
            r->pos += 4;
            return true;
        default:
            return false;
    }
}
