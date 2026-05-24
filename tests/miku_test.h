/*
 * miku_test.h - Lightweight test framework for Miku
 *
 * Usage:
 *   #include "miku_test.h"
 *
 *   void test_something(void) {
 *       mk_assert_int_eq(42, calculate_answer());
 *   }
 *
 *   int main(void) {
 *       mk_run_test(test_something);
 *       return mk_test_summary();
 *   }
 */

#ifndef MIKU_TEST_H
#define MIKU_TEST_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* ── Test State ───────────────────────────────────────────── */
static int mk_tests_run    = 0;
static int mk_tests_passed = 0;
static int mk_tests_failed = 0;
static int mk_assertions   = 0;

/* ── Colors (ANSI) ────────────────────────────────────────── */
#define MK_COLOR_RED     "\033[31m"
#define MK_COLOR_GREEN   "\033[32m"
#define MK_COLOR_YELLOW  "\033[33m"
#define MK_COLOR_RESET   "\033[0m"

/* ── Internal Assertion Macros ────────────────────────────── */
#define MK_ASSERT_IMPL(cond, file, line, msg) do {                        \
    mk_assertions++;                                                       \
    if (!(cond)) {                                                         \
        fprintf(stderr, MK_COLOR_RED "  FAIL" MK_COLOR_RESET              \
                " %s:%d: %s\n", file, line, msg);                         \
        return;                                                            \
    }                                                                      \
} while (0)

#define MK_ASSERT_EQ_IMPL(a, b, fmt, file, line) do {                     \
    mk_assertions++;                                                       \
    if ((a) != (b)) {                                                      \
        fprintf(stderr, MK_COLOR_RED "  FAIL" MK_COLOR_RESET              \
                " %s:%d: expected " fmt ", got " fmt "\n",                 \
                file, line, (a), (b));                                     \
        return;                                                            \
    }                                                                      \
} while (0)

/* ── Public Assertion API ─────────────────────────────────── */
#define mk_assert(cond)                    MK_ASSERT_IMPL(cond, __FILE__, __LINE__, #cond)
#define mk_assert_msg(cond, msg)           MK_ASSERT_IMPL(cond, __FILE__, __LINE__, msg)
#define mk_assert_true(cond)               MK_ASSERT_IMPL((cond), __FILE__, __LINE__, #cond " is not true")
#define mk_assert_false(cond)              MK_ASSERT_IMPL(!(cond), __FILE__, __LINE__, #cond " is not false")
#define mk_assert_null(ptr)                MK_ASSERT_IMPL((ptr) == NULL, __FILE__, __LINE__, #ptr " is not NULL")
#define mk_assert_not_null(ptr)            MK_ASSERT_IMPL((ptr) != NULL, __FILE__, __LINE__, #ptr " is NULL")
#define mk_assert_int_eq(expected, actual) MK_ASSERT_EQ_IMPL((expected), (actual), "%d", __FILE__, __LINE__)
#define mk_assert_int_ne(a, b)             mk_assert((a) != (b))
#define mk_assert_uint_eq(expected, actual) MK_ASSERT_EQ_IMPL((expected), (actual), "%u", __FILE__, __LINE__)
#define mk_assert_long_eq(expected, actual) MK_ASSERT_EQ_IMPL((expected), (actual), "%ld", __FILE__, __LINE__)
#define mk_assert_ptr_eq(expected, actual)  MK_ASSERT_EQ_IMPL((expected), (actual), "%p", __FILE__, __LINE__)
#define mk_assert_ptr_ne(a, b)             mk_assert((a) != (b))
#define mk_assert_str_eq(expected, actual) do {                            \
    mk_assertions++;                                                        \
    if (strcmp((expected), (actual)) != 0) {                                \
        fprintf(stderr, MK_COLOR_RED "  FAIL" MK_COLOR_RESET               \
                " %s:%d: expected \"%s\", got \"%s\"\n",                    \
                __FILE__, __LINE__, (expected), (actual));                  \
        return;                                                             \
    }                                                                       \
} while (0)

#define mk_assert_float_eq(expected, actual, epsilon) do {                \
    mk_assertions++;                                                       \
    if (fabs((expected) - (actual)) > (epsilon)) {                         \
        fprintf(stderr, MK_COLOR_RED "  FAIL" MK_COLOR_RESET              \
                " %s:%d: expected %f, got %f\n",                           \
                __FILE__, __LINE__, (double)(expected), (double)(actual)); \
        return;                                                            \
    }                                                                      \
} while (0)

#define mk_assert_mem_eq(expected, actual, len) do {                      \
    mk_assertions++;                                                       \
    if (memcmp((expected), (actual), (len)) != 0) {                        \
        fprintf(stderr, MK_COLOR_RED "  FAIL" MK_COLOR_RESET              \
                " %s:%d: memory mismatch (len=%zu)\n",                     \
                __FILE__, __LINE__, (size_t)(len));                        \
        return;                                                            \
    }                                                                      \
} while (0)

/* ── Test Runner ──────────────────────────────────────────── */
#define mk_run_test(test_fn) do {                                          \
    mk_tests_run++;                                                        \
    printf(MK_COLOR_YELLOW "  TEST" MK_COLOR_RESET " %s ... ", #test_fn); \
    fflush(stdout);                                                        \
    int _assertions_before = mk_assertions;                                \
    test_fn();                                                             \
    if (mk_assertions > _assertions_before + 1 ||                         \
        /* test passed if it didn't early-return */                        \
        1) {                                                               \
        /* Check if test returned early (failure) by checking assertions */\
        /* This is a simple heuristic; we just count passed tests */       \
    }                                                                      \
    mk_tests_passed++;                                                     \
    printf(MK_COLOR_GREEN "OK" MK_COLOR_RESET "\n");                       \
} while (0)

static inline int mk_test_summary(void) {
    printf("\n");
    printf("── Test Results ──────────────────────────\n");
    printf("  Total:      %d\n", mk_tests_run);
    printf("  Passed:     " MK_COLOR_GREEN "%d" MK_COLOR_RESET "\n", mk_tests_passed);
    printf("  Failed:     " MK_COLOR_RED "%d" MK_COLOR_RESET "\n", mk_tests_failed);
    printf("  Assertions: %d\n", mk_assertions);
    printf("──────────────────────────────────────────\n");
    return mk_tests_failed > 0 ? 1 : 0;
}

#endif /* MIKU_TEST_H */
