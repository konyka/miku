/*
 * miku_common.h - Platform detection, compiler features, common types
 *
 * Compatible with C99 through C23.
 * Part of the Miku IM Server project.
 */

#ifndef MIKU_COMMON_H
#define MIKU_COMMON_H

#if defined(MIKU_POSIX) || defined(__linux__)
#  ifndef _POSIX_C_SOURCE
#    define _POSIX_C_SOURCE 200809L
#  endif
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#endif

/* ── C Standard Version ──────────────────────────────────── */
#if defined(__STDC_VERSION__)
#  if __STDC_VERSION__ >= 202311L
#    define MIKU_C23 1
#  elif __STDC_VERSION__ >= 201710L
#    define MIKU_C17 1
#  elif __STDC_VERSION__ >= 201112L
#    define MIKU_C11 1
#  elif __STDC_VERSION__ >= 199901L
#    define MIKU_C99 1
#  endif
#else
#  error "A C99-compliant compiler is required."
#endif

/* ── Compiler Detection ──────────────────────────────────── */
#if defined(__GNUC__)
#  define MIKU_COMPILER_GCC 1
#  define MIKU_GCC_VERSION (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#elif defined(__clang__)
#  define MIKU_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#  define MIKU_COMPILER_MSVC 1
#endif

/* ── Platform Detection ──────────────────────────────────── */
#if defined(__linux__)
#  define MIKU_LINUX   1
#  define MIKU_POSIX   1
#elif defined(__APPLE__) && defined(__MACH__)
#  define MIKU_MACOS   1
#  define MIKU_POSIX   1
#elif defined(_WIN32)
#  define MIKU_WINDOWS 1
#  if defined(_WIN64)
#    define MIKU_WINDOWS_64 1
#  endif
#elif defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  define MIKU_BSD     1
#  define MIKU_POSIX   1
#endif

/* ── Export / Visibility ─────────────────────────────────── */
#if defined(MIKU_WINDOWS) && defined(BUILDING_SHARED)
#  define MIKU_API __declspec(dllexport)
#elif defined(MIKU_WINDOWS)
#  define MIKU_API __declspec(dllimport)
#elif defined(__GNUC__) && defined(BUILDING_SHARED)
#  define MIKU_API __attribute__((visibility("default")))
#else
#  define MIKU_API
#endif

/* ── Compiler Hints ──────────────────────────────────────── */
#if defined(__GNUC__) || defined(__clang__)
#  define MIKU_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define MIKU_UNLIKELY(x) __builtin_expect(!!(x), 0)
#  define MIKU_UNUSED       __attribute__((unused))
#  define MIKU_ALIGNED(x)   __attribute__((aligned(x)))
#  define MIKU_PACKED        __attribute__((packed))
#  define MIKU_NORETURN      __attribute__((noreturn))
#  define MIKU_MALLOC        __attribute__((malloc))
#  define MIKU_NONNULL(...)  __attribute__((nonnull(__VA_ARGS__)))
#  define MIKU_FORMAT(fmt, args) __attribute__((format(printf, fmt, args)))
#  define MIKU_FORCEINLINE  __attribute__((always_inline)) static inline
#  define MIKU_NOINLINE     __attribute__((noinline))
#  define MIKU_WEAK         __attribute__((weak))
#  define MIKU_FALLTHROUGH  __attribute__((fallthrough))
#elif defined(_MSC_VER)
#  define MIKU_LIKELY(x)   (x)
#  define MIKU_UNLIKELY(x) (x)
#  define MIKU_UNUSED       __pragma(warning(suppress:4100))
#  define MIKU_ALIGNED(x)   __declspec(align(x))
#  define MIKU_PACKED
#  define MIKU_NORETURN      __declspec(noreturn)
#  define MIKU_MALLOC        __declspec(restrict)
#  define MIKU_NONNULL(...)
#  define MIKU_FORMAT(fmt, args)
#  define MIKU_FORCEINLINE  __forceinline static
#  define MIKU_NOINLINE     __declspec(noinline)
#  define MIKU_WEAK
#  define MIKU_FALLTHROUGH
#else
#  define MIKU_LIKELY(x)   (x)
#  define MIKU_UNLIKELY(x) (x)
#  define MIKU_UNUSED
#  define MIKU_ALIGNED(x)
#  define MIKU_PACKED
#  define MIKU_NORETURN
#  define MIKU_MALLOC
#  define MIKU_NONNULL(...)
#  define MIKU_FORMAT(fmt, args)
#  define MIKU_FORCEINLINE  static inline
#  define MIKU_NOINLINE
#  define MIKU_WEAK
#  define MIKU_FALLTHROUGH
#endif

/* ── C11 Atomics (with C99 fallback) ─────────────────────── */
#if defined(MIKU_C11) || defined(MIKU_C17) || defined(MIKU_C23)
#  include <stdatomic.h>
#  define MIKU_HAS_STDATOMIC 1
#else
   /* C99 fallback: use compiler builtins or pthread mutexes */
#  if defined(__GNUC__) || defined(__clang__)
#    define MIKU_HAS_GCC_ATOMIC 1
#  endif
#endif

/* ── Static Assert ───────────────────────────────────────── */
#if defined(MIKU_C23)
   /* C23 has static_assert as a keyword; _Static_assert still works */
#  define MIKU_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#elif defined(MIKU_C11)
#  define MIKU_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#else
   /* C99 fallback */
#  define MIKU_STATIC_ASSERT_CONCAT_(a, b) a##b
#  define MIKU_STATIC_ASSERT_CONCAT(a, b)  MIKU_STATIC_ASSERT_CONCAT_(a, b)
#  define MIKU_STATIC_ASSERT(cond, msg) \
     typedef char MIKU_STATIC_ASSERT_CONCAT(static_assert_, __LINE__) \
       [(cond) ? 1 : -1]
#endif

/* ── Standard Includes ───────────────────────────────────── */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <limits.h>

#if defined(MIKU_POSIX)
#  include <unistd.h>
#  include <errno.h>
#  include <pthread.h>
#  include <sys/types.h>
#  include <sys/socket.h>
#  include <netinet/in.h>
#  include <arpa/inet.h>
#  include <netdb.h>
#  include <fcntl.h>
#  include <time.h>
#  include <signal.h>
#  include <sys/uio.h>    /* scatter-gather I/O */
#  include <ucontext.h>   /* coroutine support */
#endif

#if defined(MIKU_WINDOWS)
#  include <winsock2.h>
#  include <ws2tcpip.h>
#  include <windows.h>
#endif

/* ── Common Types ────────────────────────────────────────── */

/* Error codes */
typedef enum miku_status_e {
    MK_OK              =  0,
    MK_ERR_UNKNOWN     = -1,
    MK_ERR_MEMORY      = -2,
    MK_ERR_INVALID_ARG = -3,
    MK_ERR_NOT_FOUND   = -4,
    MK_ERR_TIMEOUT     = -5,
    MK_ERR_IO          = -6,
    MK_ERR_PERMISSION  = -7,
    MK_ERR_EXISTS      = -8,
    MK_ERR_BUSY        = -9,
    MK_ERR_OVERFLOW    = -10,
    MK_ERR_PROTOCOL    = -11,
    MK_ERR_DISCONNECT  = -12,
} miku_status_t;

/* String slice (non-owning view into a string) */
typedef struct miku_str_s {
    const char *data;
    size_t      len;
} miku_str_t;

/* Byte buffer (owning) */
typedef struct miku_buf_s {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} miku_buf_t;

/* Function pointer types */
typedef void (*miku_cb_fn)(void *ctx);
typedef void (*miku_free_fn)(void *ptr);
typedef void (*miku_task_fn)(void *arg);

/* ── Utility Macros ──────────────────────────────────────── */
#define MIKU_ARRAY_SIZE(arr)    (sizeof(arr) / sizeof((arr)[0]))
#define MIKU_CONTAINER_OF(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#define MIKU_MAX(a, b)          ((a) > (b) ? (a) : (b))
#define MIKU_MIN(a, b)          ((a) < (b) ? (a) : (b))
#define MIKU_CLAMP(x, lo, hi)   MIKU_MAX(lo, MIKU_MIN(x, hi))
#define MIKU_ROUND_UP(x, align) (((x) + (align) - 1) & ~((size_t)(align) - 1))
#define MIKU_SWAP(a, b, type)   do { type _tmp = (a); (a) = (b); (b) = _tmp; } while(0)
#define MIKU_STRINGIFY(x)       #x
#define MIKU_TOSTRING(x)        MIKU_STRINGIFY(x)
#define MIKU_STRING(literal)    ((miku_str_t){ literal, sizeof(literal) - 1 })
#define MIKU_VERSION             "0.1.0"

/* ── Bit Operations ──────────────────────────────────────── */
MIKU_STATIC_ASSERT(sizeof(uint64_t) == 8, "uint64_t must be 8 bytes");

MIKU_FORCEINLINE uint64_t miku_bswap64(uint64_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(val);
#elif defined(_MSC_VER)
    return _byteswap_uint64(val);
#else
    return ((val & 0xFF00000000000000ULL) >> 56) |
           ((val & 0x00FF000000000000ULL) >> 40) |
           ((val & 0x0000FF0000000000ULL) >> 24) |
           ((val & 0x000000FF00000000ULL) >>  8) |
           ((val & 0x00000000FF000000ULL) <<  8) |
           ((val & 0x0000000000FF0000ULL) << 24) |
           ((val & 0x000000000000FF00ULL) << 40) |
           ((val & 0x00000000000000FFULL) << 56);
#endif
}

MIKU_FORCEINLINE uint32_t miku_bswap32(uint32_t val) {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap32(val);
#elif defined(_MSC_VER)
    return _byteswap_ulong(val);
#else
    return ((val & 0xFF000000) >> 24) |
           ((val & 0x00FF0000) >>  8) |
           ((val & 0x0000FF00) <<  8) |
           ((val & 0x000000FF) << 24);
#endif
}

/* Big-endian read/write (network byte order) */
MIKU_FORCEINLINE uint16_t miku_read_be16(const uint8_t *p) {
    return (uint16_t)(p[0]) << 8 | (uint16_t)(p[1]);
}

MIKU_FORCEINLINE uint32_t miku_read_be32(const uint8_t *p) {
    return (uint32_t)(p[0]) << 24 | (uint32_t)(p[1]) << 16 |
           (uint32_t)(p[2]) << 8  | (uint32_t)(p[3]);
}

MIKU_FORCEINLINE uint64_t miku_read_be64(const uint8_t *p) {
    return (uint64_t)miku_read_be32(p) << 32 | (uint64_t)miku_read_be32(p + 4);
}

MIKU_FORCEINLINE void miku_write_be16(uint8_t *p, uint16_t val) {
    p[0] = (uint8_t)(val >> 8);
    p[1] = (uint8_t)(val);
}

MIKU_FORCEINLINE void miku_write_be32(uint8_t *p, uint32_t val) {
    p[0] = (uint8_t)(val >> 24);
    p[1] = (uint8_t)(val >> 16);
    p[2] = (uint8_t)(val >> 8);
    p[3] = (uint8_t)(val);
}

MIKU_FORCEINLINE void miku_write_be64(uint8_t *p, uint64_t val) {
    miku_write_be32(p, (uint32_t)(val >> 32));
    miku_write_be32(p + 4, (uint32_t)(val));
}

/* ── Time Utilities ──────────────────────────────────────── */
MIKU_FORCEINLINE int64_t miku_timestamp_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

MIKU_FORCEINLINE int64_t miku_timestamp_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

/* ── Defer Pattern (GCC/Clang extension) ─────────────────── */
#if defined(__GNUC__) || defined(__clang__)
#  define MIKU_DEFER(fn) \
     MIKU_UNUSED __attribute__((cleanup(fn)))
#endif

#endif /* MIKU_COMMON_H */
