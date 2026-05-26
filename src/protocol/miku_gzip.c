#include "miku_gzip.h"
#include <string.h>
#include <zlib.h>

int miku_gzip_compress(const char *in, size_t in_len,
                         char *out, size_t *out_len, int level) {
    if (!in || !out || !out_len) return -1;
    z_stream strm;
    memset(&strm, 0, sizeof(strm));
    int zlevel = Z_DEFAULT_COMPRESSION;
    if (level == MK_GZIP_BEST_SPEED) zlevel = 1;
    else if (level == MK_GZIP_BEST_COMPRESSION) zlevel = 9;

    if (deflateInit2(&strm, zlevel, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return -1;

    strm.next_in = (Bytef *)in;
    strm.avail_in = (uInt)in_len;
    strm.next_out = (Bytef *)out;
    strm.avail_out = (uInt)*out_len;

    int ret = deflate(&strm, Z_FINISH);
    *out_len = *out_len - strm.avail_out;
    deflateEnd(&strm);

    return (ret == Z_STREAM_END) ? 0 : -1;
}

int miku_gzip_decompress(const char *in, size_t in_len,
                           char *out, size_t *out_len) {
    if (!in || !out || !out_len) return -1;
    z_stream strm;
    memset(&strm, 0, sizeof(strm));

    if (inflateInit2(&strm, 15 + 16) != Z_OK) return -1;

    strm.next_in = (Bytef *)in;
    strm.avail_in = (uInt)in_len;
    strm.next_out = (Bytef *)out;
    strm.avail_out = (uInt)*out_len;

    int ret = inflate(&strm, Z_FINISH);
    *out_len = *out_len - strm.avail_out;
    inflateEnd(&strm);

    return (ret == Z_STREAM_END) ? 0 : -1;
}

bool miku_gzip_accepts_encoding(const char *accept_encoding) {
    if (!accept_encoding) return false;
    return strstr(accept_encoding, "gzip") != NULL;
}
