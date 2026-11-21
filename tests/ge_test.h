/*
 * Tiny test helper for the CTest binaries (no third-party framework).
 *
 *   GE_TEST_BEGIN("area name");
 *   GE_CHECK(cond);                 // records a failure, keeps going
 *   GE_CHECK_EQ_INT(got, want);
 *   GE_CHECK_STR(got, want);
 *   return GE_TEST_END();           // prints the tally; 0 = all passed
 *
 * One binary per area: each is its own process, so the game globals start
 * clean. A binary main() is the test; it links the game core (see
 * docs/building.md, Tests).
 */
#ifndef GE_TEST_H
#define GE_TEST_H

#include <stdio.h>
#include <string.h>

static int ge_test_checks, ge_test_fails;
static const char *ge_test_area = "";

#define GE_TEST_BEGIN(name) (ge_test_area = (name), ge_test_checks = 0, ge_test_fails = 0)

#define GE_FAIL_(...) do { \
        ge_test_fails++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } while (0)

#define GE_CHECK(cond) do { \
        ge_test_checks++; \
        if (!(cond)) GE_FAIL_("%s", #cond); \
    } while (0)

#define GE_CHECK_EQ_INT(got, want) do { \
        long long g_ = (long long)(got), w_ = (long long)(want); \
        ge_test_checks++; \
        if (g_ != w_) GE_FAIL_("%s = %lld, expected %lld", #got, g_, w_); \
    } while (0)

#define GE_CHECK_STR(got, want) do { \
        const char *g_ = (got), *w_ = (want); \
        ge_test_checks++; \
        if (!g_ || strcmp(g_, w_) != 0) GE_FAIL_("%s = \"%s\", expected \"%s\"", #got, g_ ? g_ : "(null)", w_); \
    } while (0)

#define GE_TEST_END() \
    (printf("%s: %d checks, %d failed\n", ge_test_area, ge_test_checks, ge_test_fails), \
     ge_test_fails ? 1 : 0)

#endif
