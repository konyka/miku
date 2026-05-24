#include "miku_sha1.h"
#include <string.h>

static const uint32_t K[4] = { 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6 };

#define ROTL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define CH(x, y, z)  (((x) & (y)) ^ (~(x) & (z)))
#define MAJ(x, y, z) (((x) & (y)) ^ ((x) & (z)) ^ ((y) & (z)))
#define PARITY(x, y, z) ((x) ^ (y) ^ (z))

static void sha1_block(uint32_t state[5], const uint8_t block[64]) {
    uint32_t w[80];
    for (int t = 0; t < 16; t++) {
        w[t] = ((uint32_t)block[t * 4] << 24) |
               ((uint32_t)block[t * 4 + 1] << 16) |
               ((uint32_t)block[t * 4 + 2] << 8) |
               ((uint32_t)block[t * 4 + 3]);
    }
    for (int t = 16; t < 80; t++)
        w[t] = ROTL(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];
    for (int t = 0; t < 80; t++) {
        uint32_t f, k;
        if (t < 20)      { f = CH(b, c, d);  k = K[0]; }
        else if (t < 40) { f = PARITY(b, c, d); k = K[1]; }
        else if (t < 60) { f = MAJ(b, c, d); k = K[2]; }
        else             { f = PARITY(b, c, d); k = K[3]; }
        uint32_t tmp = ROTL(a, 5) + f + e + k + w[t];
        e = d;
        d = c;
        c = ROTL(b, 30);
        b = a;
        a = tmp;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

void miku_sha1(uint8_t digest[20], const uint8_t *data, size_t len) {
    uint32_t state[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    uint64_t bitlen = (uint64_t)len * 8;

    size_t i = 0;
    uint8_t block[64];
    while (i + 64 <= len) {
        sha1_block(state, data + i);
        i += 64;
    }

    size_t rem = len - i;
    memcpy(block, data + i, rem);
    block[rem] = 0x80;
    if (rem >= 56) {
        memset(block + rem + 1, 0, 64 - rem - 1);
        sha1_block(state, block);
        memset(block, 0, 56);
    } else {
        memset(block + rem + 1, 0, 56 - rem - 1);
    }

    for (int j = 0; j < 8; j++)
        block[63 - j] = (uint8_t)(bitlen >> (j * 8));
    sha1_block(state, block);

    for (int j = 0; j < 5; j++) {
        digest[j * 4]     = (uint8_t)(state[j] >> 24);
        digest[j * 4 + 1] = (uint8_t)(state[j] >> 16);
        digest[j * 4 + 2] = (uint8_t)(state[j] >> 8);
        digest[j * 4 + 3] = (uint8_t)(state[j]);
    }
}
