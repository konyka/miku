#include "miku_uuid.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <sys/random.h>

/* UUIDs back message IDs, group IDs and token nonces, so they must be
 * unpredictable, not merely unique. Seeding an LCG from a 32-bit
 * timestamp-derived value let anyone who knew the approximate creation time
 * enumerate the whole output space.
 *
 * getrandom(2) per UUID would put a syscall on the message-ID hot path, so
 * draw a thread-local pool instead: one syscall per MK_UUID_POOL_SZ/16 UUIDs,
 * and no locking between threads. */
#define MK_UUID_POOL_SZ 4096

static _Thread_local uint8_t mk_uuid_pool[MK_UUID_POOL_SZ];
static _Thread_local size_t  mk_uuid_pool_pos = MK_UUID_POOL_SZ; /* empty */

static bool pool_fill(uint8_t *buf, size_t len) {
    size_t got = 0;
    while (got < len) {
        ssize_t r = getrandom(buf + got, len - got, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) break;
        got += (size_t)r;
    }
    if (got == len) return true;

    /* Pre-3.17 kernels and seccomp sandboxes that reject getrandom. */
    FILE *f = fopen("/dev/urandom", "rb");
    if (f) {
        size_t n = fread(buf + got, 1, len - got, f);
        fclose(f);
        got += n;
    }
    return got == len;
}

/* Last resort so ID generation degrades rather than aborting: still seeded from
 * the clock, so callers get uniqueness but not unpredictability. */
static void fallback_bytes(uint8_t *out, size_t len) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t s = (uint64_t)ts.tv_nsec * 6364136223846793005ULL
               ^ (uint64_t)ts.tv_sec * 1442695040888963407ULL
               ^ (uint64_t)(uintptr_t)out;
    for (size_t i = 0; i < len; i++) {
        s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
        out[i] = (uint8_t)((s * 2685821657736338717ULL) >> 33);
    }
}

void miku_uuid_generate_bytes(uint8_t out[16]) {
    if (mk_uuid_pool_pos + 16 > MK_UUID_POOL_SZ &&
        pool_fill(mk_uuid_pool, MK_UUID_POOL_SZ)) {
        mk_uuid_pool_pos = 0;
    }
    if (mk_uuid_pool_pos + 16 > MK_UUID_POOL_SZ) {
        fallback_bytes(out, 16);
    } else {
        memcpy(out, mk_uuid_pool + mk_uuid_pool_pos, 16);
        mk_uuid_pool_pos += 16;
    }

    /* RFC 9562 §4.4: version 4, variant 10xx. */
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
