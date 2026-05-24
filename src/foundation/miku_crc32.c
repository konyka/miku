#include "miku_crc32.h"

static uint32_t crc32_table[256];
static int crc32_table_init = 0;

static void crc32_make_table(void) {
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++)
            c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
        crc32_table[i] = c;
    }
    crc32_table_init = 1;
}

uint32_t miku_crc32_update(uint32_t crc, const uint8_t *data, size_t len) {
    if (!crc32_table_init) crc32_make_table();
    crc = ~crc;
    for (size_t i = 0; i < len; i++)
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

uint32_t miku_crc32(const uint8_t *data, size_t len) {
    return miku_crc32_update(0, data, len);
}
