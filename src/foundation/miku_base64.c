#include "miku_base64.h"

static const char b64_enc[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

size_t miku_base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap) {
    size_t needed = 4 * ((in_len + 2) / 3);
    if (!out || out_cap < needed + 1) return needed;
    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 3) {
        uint32_t n = (uint32_t)in[i] << 16;
        if (i + 1 < in_len) n |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < in_len) n |= (uint32_t)in[i + 2];
        out[j++] = b64_enc[(n >> 18) & 0x3F];
        out[j++] = b64_enc[(n >> 12) & 0x3F];
        out[j++] = (i + 1 < in_len) ? b64_enc[(n >> 6) & 0x3F] : '=';
        out[j++] = (i + 2 < in_len) ? b64_enc[n & 0x3F] : '=';
    }
    out[j] = '\0';
    return j;
}

static int b64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

size_t miku_base64_decode(const char *in, size_t in_len, uint8_t *out, size_t out_cap) {
    if (in_len % 4 != 0) return 0;
    size_t needed = (in_len / 4) * 3;
    if (in_len >= 1 && in[in_len - 1] == '=') needed--;
    if (in_len >= 2 && in[in_len - 2] == '=') needed--;
    if (!out || out_cap < needed) return needed;
    size_t j = 0;
    for (size_t i = 0; i < in_len; i += 4) {
        int a = b64_val(in[i]);
        int b = b64_val(in[i + 1]);
        int c = (in[i + 2] != '=') ? b64_val(in[i + 2]) : 0;
        int d = (in[i + 3] != '=') ? b64_val(in[i + 3]) : 0;
        uint32_t n = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        out[j++] = (uint8_t)(n >> 16);
        if (in[i + 2] != '=') out[j++] = (uint8_t)(n >> 8);
        if (in[i + 3] != '=') out[j++] = (uint8_t)n;
    }
    return j;
}
