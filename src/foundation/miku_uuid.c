#include "miku_uuid.h"
#include <stdlib.h>
#include <time.h>

void miku_uuid_generate_bytes(uint8_t out[16]) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint32_t seed = (uint32_t)(ts.tv_nsec ^ ts.tv_sec ^ (uintptr_t)&out);
    for (int i = 0; i < 16; i++) {
        seed = seed * 1103515245 + 12345;
        out[i] = (uint8_t)(seed >> 16);
    }
    out[6] = (out[6] & 0x0F) | 0x40;
    out[8] = (out[8] & 0x3F) | 0x80;
}

void miku_uuid_generate(char out[37]) {
    uint8_t bytes[16];
    miku_uuid_generate_bytes(bytes);
    static const char hex[] = "0123456789abcdef";
    int j = 0;
    for (int i = 0; i < 16; i++) {
        out[j++] = hex[bytes[i] >> 4];
        out[j++] = hex[bytes[i] & 0x0F];
        if (i == 3 || i == 5 || i == 7 || i == 9)
            out[j++] = '-';
    }
    out[36] = '\0';
}
