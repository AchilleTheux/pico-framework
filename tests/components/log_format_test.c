/*
 * Host-side tests for log levels and the line prefix.
 *
 * The prefix is built from a path that is often already going wrong, so the
 * cases that matter are the ones where the buffer is too small: a truncated
 * prefix is acceptable, a write past the end of a stack buffer while reporting
 * an error is not.
 */

#include <string.h>

#include "test.h"

#include "log_format.h"

static const char *prefix(log_level_t level, uint64_t us, const char *tag)
{
    static char buffer[64];
    log_format_prefix(buffer, sizeof(buffer), level, us, tag);
    return buffer;
}

/* ---------------------------------------------------------------------------
 * Levels
 * -------------------------------------------------------------------------*/

TEST(levels_are_ordered_from_verbose_to_severe)
{
    /* A threshold is a comparison, so the order is load-bearing. */
    CHECK(LOG_LEVEL_TRACE < LOG_LEVEL_DEBUG);
    CHECK(LOG_LEVEL_DEBUG < LOG_LEVEL_INFO);
    CHECK(LOG_LEVEL_INFO < LOG_LEVEL_WARN);
    CHECK(LOG_LEVEL_WARN < LOG_LEVEL_ERROR);
    CHECK(LOG_LEVEL_ERROR < LOG_LEVEL_NONE);
}

TEST(a_threshold_admits_its_own_level_and_above)
{
    CHECK(log_level_enabled(LOG_LEVEL_WARN, LOG_LEVEL_WARN));
    CHECK(log_level_enabled(LOG_LEVEL_ERROR, LOG_LEVEL_WARN));
    CHECK(!log_level_enabled(LOG_LEVEL_INFO, LOG_LEVEL_WARN));
    CHECK(!log_level_enabled(LOG_LEVEL_TRACE, LOG_LEVEL_WARN));
}

TEST(the_none_threshold_silences_everything)
{
    for (int level = LOG_LEVEL_TRACE; level <= LOG_LEVEL_ERROR; level++) {
        if (log_level_enabled((log_level_t)level, LOG_LEVEL_NONE)) {
            printf("    %s survived a NONE threshold\n",
                   log_level_name((log_level_t)level));
            CHECK(false);
            return;
        }
    }
}

TEST(the_none_level_is_never_emitted_itself)
{
    /* It is a threshold, not something to log at. */
    CHECK(!log_level_enabled(LOG_LEVEL_NONE, LOG_LEVEL_TRACE));
}

TEST(levels_have_names_and_initials)
{
    CHECK_EQ_STR(log_level_name(LOG_LEVEL_TRACE), "trace");
    CHECK_EQ_STR(log_level_name(LOG_LEVEL_WARN), "warn");
    CHECK_EQ_STR(log_level_name(LOG_LEVEL_NONE), "none");
    CHECK_EQ_STR(log_level_name((log_level_t)99), "unknown");

    CHECK_EQ_INT(log_level_initial(LOG_LEVEL_TRACE), 'T');
    CHECK_EQ_INT(log_level_initial(LOG_LEVEL_ERROR), 'E');
    CHECK_EQ_INT(log_level_initial((log_level_t)99), '?');
}

TEST(a_level_can_be_parsed_from_a_name_or_a_letter)
{
    /* A console types every character, so a single letter is worth accepting. */
    log_level_t level = LOG_LEVEL_NONE;

    CHECK(log_level_parse("warn", &level));
    CHECK_EQ_INT(level, LOG_LEVEL_WARN);
    CHECK(log_level_parse("WARN", &level));
    CHECK_EQ_INT(level, LOG_LEVEL_WARN);
    CHECK(log_level_parse("w", &level));
    CHECK_EQ_INT(level, LOG_LEVEL_WARN);
    CHECK(log_level_parse("E", &level));
    CHECK_EQ_INT(level, LOG_LEVEL_ERROR);
    CHECK(log_level_parse("trace", &level));
    CHECK_EQ_INT(level, LOG_LEVEL_TRACE);
}

TEST(an_unparseable_level_leaves_the_target_alone)
{
    log_level_t level = LOG_LEVEL_INFO;

    CHECK(!log_level_parse("verbose", &level));
    CHECK(!log_level_parse("", &level));
    CHECK(!log_level_parse("wa", &level));       /* a prefix is not a name */
    CHECK(!log_level_parse(NULL, &level));
    CHECK_EQ_INT(level, LOG_LEVEL_INFO);
}

/* ---------------------------------------------------------------------------
 * The prefix
 * -------------------------------------------------------------------------*/

TEST(a_prefix_carries_the_time_the_level_and_the_tag)
{
    CHECK_EQ_STR(prefix(LOG_LEVEL_WARN, 12345678u, "servo"),
                 "[    12.345] W servo: ");
}

TEST(a_prefix_without_a_tag_omits_the_colon)
{
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 1000u, NULL), "[     0.001] I ");
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 1000u, ""), "[     0.001] I ");
}

TEST(the_timestamp_is_seconds_and_milliseconds)
{
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 0u, NULL),         "[     0.000] I ");
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 999u, NULL),       "[     0.000] I ");
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 1000u, NULL),      "[     0.001] I ");
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 1000000u, NULL),   "[     1.000] I ");
    CHECK_EQ_STR(prefix(LOG_LEVEL_INFO, 61500000u, NULL),  "[    61.500] I ");
}

TEST(lines_stay_in_columns_for_hours)
{
    /* The timestamp is padded so output remains readable over a long run. */
    const size_t short_run = strlen(prefix(LOG_LEVEL_INFO, 1000u, NULL));
    const size_t long_run = strlen(prefix(LOG_LEVEL_INFO, 3600u * 1000000u, NULL));
    CHECK_EQ_INT(short_run, long_run);
}

TEST(a_very_long_uptime_does_not_break_the_format)
{
    /* Nothing here is likely to run for a year, but a timestamp that overflows
       its field must still produce a valid string. */
    const char *line = prefix(LOG_LEVEL_INFO, 400ull * 24 * 3600 * 1000000ull, NULL);
    CHECK(strlen(line) > 10);
    CHECK(line[0] == '[');
}

TEST(a_prefix_is_truncated_rather_than_overrunning)
{
    /*
     * The case that matters. This is called while something is already going
     * wrong, so writing past a stack buffer would turn a reported problem into
     * an unreported one.
     */
    for (size_t capacity = 1; capacity < 32; capacity++) {
        char buffer[64];
        memset(buffer, 0x7F, sizeof(buffer));

        const size_t written = log_format_prefix(buffer, capacity, LOG_LEVEL_WARN,
                                                 12345678u, "servo");

        if (written >= capacity) {
            printf("    capacity %u: reported %u written\n", (unsigned)capacity,
                   (unsigned)written);
            CHECK(false);
            return;
        }
        /* Terminated at the reported length, which is where it belongs whether
           the prefix fitted or was cut short. */
        if (buffer[written] != '\0') {
            printf("    capacity %u: not terminated at %u\n", (unsigned)capacity,
                   (unsigned)written);
            CHECK(false);
            return;
        }
        /* Nothing beyond the stated capacity was touched. */
        if (buffer[capacity] != 0x7F) {
            printf("    capacity %u: wrote past the end\n", (unsigned)capacity);
            CHECK(false);
            return;
        }
        if (strlen(buffer) != written) {
            CHECK_EQ_INT(strlen(buffer), written);
            return;
        }
    }
}

TEST(a_zero_capacity_writes_nothing)
{
    char buffer[4] = { 0x7F, 0x7F, 0x7F, 0x7F };

    CHECK_EQ_INT(log_format_prefix(buffer, 0, LOG_LEVEL_INFO, 0, NULL), 0);
    CHECK_EQ_INT(buffer[0], 0x7F);
    CHECK_EQ_INT(log_format_prefix(NULL, 16, LOG_LEVEL_INFO, 0, NULL), 0);
}

TEST(the_reported_length_matches_the_string)
{
    char buffer[64];
    const size_t written = log_format_prefix(buffer, sizeof(buffer),
                                             LOG_LEVEL_ERROR, 5000000u, "flash");
    CHECK_EQ_INT(written, strlen(buffer));
    CHECK_EQ_STR(buffer, "[     5.000] E flash: ");
}

TEST_MAIN(
    RUN(levels_are_ordered_from_verbose_to_severe);
    RUN(a_threshold_admits_its_own_level_and_above);
    RUN(the_none_threshold_silences_everything);
    RUN(the_none_level_is_never_emitted_itself);
    RUN(levels_have_names_and_initials);
    RUN(a_level_can_be_parsed_from_a_name_or_a_letter);
    RUN(an_unparseable_level_leaves_the_target_alone);

    RUN(a_prefix_carries_the_time_the_level_and_the_tag);
    RUN(a_prefix_without_a_tag_omits_the_colon);
    RUN(the_timestamp_is_seconds_and_milliseconds);
    RUN(lines_stay_in_columns_for_hours);
    RUN(a_very_long_uptime_does_not_break_the_format);
    RUN(a_prefix_is_truncated_rather_than_overrunning);
    RUN(a_zero_capacity_writes_nothing);
    RUN(the_reported_length_matches_the_string);
)
