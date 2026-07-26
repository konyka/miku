#include "miku_test.h"

extern void run_benchmarks(void);

int mk_tests_run    = 0;
int mk_tests_passed = 0;
int mk_tests_failed = 0;
int mk_assertions   = 0;
int mk_test_failed  = 0;

int main(void) {
    run_benchmarks();
    return mk_test_summary();
}
