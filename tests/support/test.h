/*
 * A minimal host-side unit test harness.
 *
 * The framework deliberately downloads no dependencies (DESIGN_DOC.md section
 * 22), so this is the whole thing: a handful of macros that record failures
 * and a main() that reports them through the process exit status, which is all
 * CTest needs.
 *
 * Usage:
 *
 *     #include "test.h"
 *
 *     TEST(black_is_all_zeroes)
 *     {
 *         CHECK_EQ_U32(ws2812_color_to_wire(WS2812_COLOR_BLACK, false), 0);
 *     }
 *
 *     TEST_MAIN(
 *         RUN(black_is_all_zeroes);
 *     )
 */

#ifndef PICO_FRAMEWORK_TEST_H
#define PICO_FRAMEWORK_TEST_H

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int test_failures;        /* failing checks in the current test */
static int test_total_failures;  /* failing checks overall */
static int test_cases_run;
static int test_cases_failed;

/* Element count of an array. Named with a trailing underscore to stay clear
   of the SDK's count_of(), which host tests do not have. */
#define count_of_(array) (sizeof(array) / sizeof((array)[0]))

#define TEST(name) static void name(void)

#define TEST_FAIL_(fmt, ...)                                                   \
    do {                                                                       \
        test_failures++;                                                       \
        printf("    %s:%d: " fmt "\n", __FILE__, __LINE__, __VA_ARGS__);       \
    } while (0)

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            TEST_FAIL_("expected %s", #cond);                                  \
        }                                                                      \
    } while (0)

#define CHECK_EQ_U32(actual, expected)                                         \
    do {                                                                       \
        const uint32_t actual_ = (uint32_t)(actual);                           \
        const uint32_t expected_ = (uint32_t)(expected);                       \
        if (actual_ != expected_) {                                            \
            TEST_FAIL_("%s: expected 0x%08" PRIx32 ", got 0x%08" PRIx32,       \
                       #actual, expected_, actual_);                           \
        }                                                                      \
    } while (0)

#define CHECK_EQ_INT(actual, expected)                                         \
    do {                                                                       \
        const long actual_ = (long)(actual);                                   \
        const long expected_ = (long)(expected);                               \
        if (actual_ != expected_) {                                            \
            TEST_FAIL_("%s: expected %ld, got %ld", #actual, expected_, actual_); \
        }                                                                      \
    } while (0)

#define CHECK_EQ_STR(actual, expected)                                         \
    do {                                                                       \
        const char *actual_ = (actual);                                        \
        const char *expected_ = (expected);                                    \
        if (actual_ == NULL || strcmp(actual_, expected_) != 0) {              \
            TEST_FAIL_("%s: expected \"%s\", got \"%s\"", #actual, expected_,  \
                       actual_ ? actual_ : "(null)");                          \
        }                                                                      \
    } while (0)

static void test_run(const char *name, void (*fn)(void))
{
    test_failures = 0;
    test_cases_run++;

    fn();

    if (test_failures != 0) {
        test_cases_failed++;
        test_total_failures += test_failures;
        printf("  FAIL %s (%d check%s)\n", name, test_failures,
               test_failures == 1 ? "" : "s");
    } else {
        printf("  ok   %s\n", name);
    }
}

#define RUN(name) test_run(#name, name)

static int test_report(void)
{
    printf("%d test%s, %d failed (%d failing check%s)\n",
           test_cases_run, test_cases_run == 1 ? "" : "s",
           test_cases_failed, test_total_failures,
           test_total_failures == 1 ? "" : "s");
    return test_cases_failed == 0 ? 0 : 1;
}

#define TEST_MAIN(body)                                                        \
    int main(void)                                                             \
    {                                                                          \
        body                                                                   \
        return test_report();                                                  \
    }

#endif /* PICO_FRAMEWORK_TEST_H */
