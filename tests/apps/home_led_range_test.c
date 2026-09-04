/* Host-side tests for home_led's inclusive, one-based LED selection. */

#include "test.h"

#include "led_range.h"

TEST(the_default_is_the_complete_strip)
{
    led_range_t range;

    led_range_init(&range, 300u);

    CHECK_EQ_INT(led_range_first(&range), 1);
    CHECK_EQ_INT(led_range_last(&range), 300);
    CHECK_EQ_INT(led_range_begin(&range), 0);
    CHECK_EQ_INT(led_range_length(&range), 300);
}

TEST(an_inclusive_public_range_becomes_a_half_open_render_slice)
{
    led_range_t range;

    led_range_init(&range, 300u);
    CHECK(led_range_set(&range, 75u, 180u));

    CHECK_EQ_INT(led_range_first(&range), 75);
    CHECK_EQ_INT(led_range_last(&range), 180);
    CHECK_EQ_INT(led_range_begin(&range), 74);
    CHECK_EQ_INT(led_range_length(&range), 106);
}

TEST(an_invalid_atomic_range_is_refused_without_changing_state)
{
    led_range_t range;

    led_range_init(&range, 20u);
    CHECK(led_range_set(&range, 5u, 10u));
    const uint32_t generation = range.generation;

    CHECK(!led_range_set(&range, 11u, 10u));
    CHECK(!led_range_set(&range, 0u, 10u));
    CHECK(!led_range_set(&range, 1u, 21u));
    CHECK_EQ_INT(led_range_first(&range), 5);
    CHECK_EQ_INT(led_range_last(&range), 10);
    CHECK_EQ_U32(range.generation, generation);
}

TEST(the_endpoint_that_crosses_carries_the_other_with_it)
{
    led_range_t range;

    led_range_init(&range, 300u);
    CHECK(led_range_set(&range, 20u, 80u));

    led_range_set_first(&range, 100u);
    CHECK_EQ_INT(led_range_first(&range), 100);
    CHECK_EQ_INT(led_range_last(&range), 100);

    led_range_set_last(&range, 40u);
    CHECK_EQ_INT(led_range_first(&range), 40);
    CHECK_EQ_INT(led_range_last(&range), 40);
}

TEST(individual_endpoints_are_clamped_to_the_installation)
{
    led_range_t range;

    led_range_init(&range, 12u);
    led_range_set_first(&range, 999u);
    CHECK_EQ_INT(led_range_first(&range), 12);
    CHECK_EQ_INT(led_range_last(&range), 12);

    led_range_set_last(&range, 0u);
    CHECK_EQ_INT(led_range_first(&range), 1);
    CHECK_EQ_INT(led_range_last(&range), 1);
}

TEST(restore_rejects_corrupt_storage_to_the_safe_full_default)
{
    led_range_t range;

    led_range_restore(&range, 32u, 25u, 12u);
    CHECK_EQ_INT(led_range_first(&range), 1);
    CHECK_EQ_INT(led_range_last(&range), 32);

    led_range_restore(&range, 32u, 8u, 14u);
    CHECK_EQ_INT(led_range_first(&range), 8);
    CHECK_EQ_INT(led_range_last(&range), 14);
}

TEST(no_strip_and_null_inputs_are_survivable)
{
    led_range_t range;

    led_range_init(&range, 0u);
    led_range_set_first(&range, 1u);
    led_range_set_last(&range, 1u);
    CHECK(!led_range_set(&range, 1u, 1u));
    CHECK_EQ_INT(led_range_length(&range), 0);

    led_range_init(NULL, 10u);
    led_range_restore(NULL, 10u, 1u, 10u);
    led_range_set_first(NULL, 1u);
    led_range_set_last(NULL, 1u);
    CHECK(!led_range_set(NULL, 1u, 1u));
    CHECK_EQ_INT(led_range_first(NULL), 0);
    CHECK_EQ_INT(led_range_last(NULL), 0);
}

TEST_MAIN(
    RUN(the_default_is_the_complete_strip);
    RUN(an_inclusive_public_range_becomes_a_half_open_render_slice);
    RUN(an_invalid_atomic_range_is_refused_without_changing_state);
    RUN(the_endpoint_that_crosses_carries_the_other_with_it);
    RUN(individual_endpoints_are_clamped_to_the_installation);
    RUN(restore_rejects_corrupt_storage_to_the_safe_full_default);
    RUN(no_strip_and_null_inputs_are_survivable);
)
