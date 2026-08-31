/*
 * Host-side tests for the configuration record format.
 *
 * The buffer is meant to be a valid image of the whole set at every moment, so
 * that saving it is one write. The tests that earn their place are the ones
 * that would break that: resizing a value in the middle, removing one, filling
 * the buffer exactly, and adopting a run of records that was truncated —
 * which is precisely what an interrupted save leaves behind.
 */

#include <string.h>

#include "test.h"

#include "config_store.h"

#define CAPACITY 256u

static uint8_t buffer[CAPACITY];
static config_store_t store;

static void setup(void)
{
    memset(buffer, 0, sizeof(buffer));
    CHECK_EQ_INT(config_store_init(&store, buffer, sizeof(buffer)), CONFIG_OK);
}

static const char *get_string(const char *key)
{
    static char value[64];
    config_get_string(&store, key, value, sizeof(value), "<absent>");
    return value;
}

/* ---------------------------------------------------------------------------
 * The basics
 * -------------------------------------------------------------------------*/

TEST(a_value_comes_back)
{
    setup();
    CHECK_EQ_INT(config_set_string(&store, "wifi_ssid", "robot-net"), CONFIG_OK);
    CHECK_EQ_STR(get_string("wifi_ssid"), "robot-net");
}

TEST(an_absent_key_yields_the_fallback)
{
    setup();
    CHECK_EQ_STR(get_string("nothing"), "<absent>");

    uint32_t value = 0;
    CHECK_EQ_INT(config_get_u32(&store, "nothing", &value, 42), CONFIG_ERR_NOT_FOUND);
    CHECK_EQ_U32(value, 42u);
}

TEST(several_keys_coexist)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_set_string(&store, "c", "three");

    CHECK_EQ_STR(get_string("a"), "one");
    CHECK_EQ_STR(get_string("b"), "two");
    CHECK_EQ_STR(get_string("c"), "three");
    CHECK_EQ_INT(config_count(&store), 3);
}

TEST(a_thirty_two_bit_value_round_trips)
{
    setup();
    static const uint32_t values[] = { 0, 1, 0xFF, 0x100, 0x12345678, 0xFFFFFFFF };

    for (unsigned i = 0; i < count_of_(values); i++) {
        CHECK_EQ_INT(config_set_u32(&store, "n", values[i]), CONFIG_OK);
        uint32_t got = 0;
        CHECK_EQ_INT(config_get_u32(&store, "n", &got, 0), CONFIG_OK);
        CHECK_EQ_U32(got, values[i]);
    }
}

TEST(a_zero_length_value_is_not_the_same_as_absent)
{
    setup();
    CHECK_EQ_INT(config_set(&store, "flag", NULL, 0), CONFIG_OK);

    CHECK(config_has(&store, "flag"));
    CHECK(!config_has(&store, "other"));

    uint8_t length = 0xFF;
    CHECK_EQ_INT(config_get(&store, "flag", NULL, 0, &length), CONFIG_OK);
    CHECK_EQ_INT(length, 0);
}

/* ---------------------------------------------------------------------------
 * Rewriting, which is where the buffer image can break
 * -------------------------------------------------------------------------*/

TEST(overwriting_a_value_with_the_same_length_keeps_the_neighbours)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_set_string(&store, "c", "three");

    config_set_string(&store, "b", "TWO");

    CHECK_EQ_STR(get_string("a"), "one");
    CHECK_EQ_STR(get_string("b"), "TWO");
    CHECK_EQ_STR(get_string("c"), "three");
    CHECK_EQ_INT(config_count(&store), 3);
}

TEST(growing_a_value_in_the_middle_keeps_the_neighbours)
{
    /* The tail has to move up. Getting this wrong corrupts whatever follows,
       and the key that was written looks fine. */
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_set_string(&store, "c", "three");

    config_set_string(&store, "b", "a much longer value than before");

    CHECK_EQ_STR(get_string("a"), "one");
    CHECK_EQ_STR(get_string("b"), "a much longer value than before");
    CHECK_EQ_STR(get_string("c"), "three");
    CHECK_EQ_INT(config_count(&store), 3);
}

TEST(shrinking_a_value_in_the_middle_keeps_the_neighbours)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "a much longer value than before");
    config_set_string(&store, "c", "three");

    config_set_string(&store, "b", "x");

    CHECK_EQ_STR(get_string("a"), "one");
    CHECK_EQ_STR(get_string("b"), "x");
    CHECK_EQ_STR(get_string("c"), "three");
    CHECK_EQ_INT(config_count(&store), 3);
}

TEST(removing_a_key_closes_the_gap)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_set_string(&store, "c", "three");

    const size_t before = config_store_used(&store);
    CHECK_EQ_INT(config_remove(&store, "b"), CONFIG_OK);

    CHECK(config_store_used(&store) < before);
    CHECK_EQ_STR(get_string("a"), "one");
    CHECK_EQ_STR(get_string("c"), "three");
    CHECK(!config_has(&store, "b"));
    CHECK_EQ_INT(config_count(&store), 2);
}

TEST(removing_the_first_and_last_keys_works)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_set_string(&store, "c", "three");

    CHECK_EQ_INT(config_remove(&store, "a"), CONFIG_OK);
    CHECK_EQ_INT(config_remove(&store, "c"), CONFIG_OK);

    CHECK_EQ_INT(config_count(&store), 1);
    CHECK_EQ_STR(get_string("b"), "two");
}

TEST(removing_every_key_empties_the_store)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_string(&store, "b", "two");
    config_remove(&store, "a");
    config_remove(&store, "b");

    CHECK_EQ_INT(config_store_used(&store), 0);
    CHECK_EQ_INT(config_count(&store), 0);
}

TEST(removing_an_absent_key_changes_nothing)
{
    setup();
    config_set_string(&store, "a", "one");
    const size_t before = config_store_used(&store);

    CHECK_EQ_INT(config_remove(&store, "b"), CONFIG_ERR_NOT_FOUND);
    CHECK_EQ_INT(config_store_used(&store), before);
}

TEST(many_rewrites_leave_the_store_consistent)
{
    /* Exercises the shuffling hard: values grow and shrink repeatedly while
       neighbours must stay intact. */
    setup();
    config_set_string(&store, "before", "fixed");
    config_set_string(&store, "target", "x");
    config_set_string(&store, "after", "fixed too");

    for (unsigned i = 0; i < 200; i++) {
        char value[64];
        const unsigned length = 1u + (i % 40u);
        memset(value, 'a' + (int)(i % 26u), length);
        value[length] = '\0';

        if (config_set_string(&store, "target", value) != CONFIG_OK) {
            printf("    iteration %u could not set a %u-byte value\n", i, length);
            CHECK(false);
            return;
        }
        if (strcmp(get_string("before"), "fixed") != 0 ||
            strcmp(get_string("after"), "fixed too") != 0 ||
            strcmp(get_string("target"), value) != 0) {
            printf("    iteration %u corrupted the store\n", i);
            CHECK(false);
            return;
        }
    }
    CHECK_EQ_INT(config_count(&store), 3);
}

/* ---------------------------------------------------------------------------
 * Limits
 * -------------------------------------------------------------------------*/

TEST(a_full_store_refuses_rather_than_overflows)
{
    uint8_t small[32];
    config_store_t tiny;
    config_store_init(&tiny, small, sizeof(small));

    unsigned added = 0;
    for (unsigned i = 0; i < 100; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%u", i);
        if (config_set_string(&tiny, key, "value") != CONFIG_OK) {
            break;
        }
        added++;
    }

    CHECK(added > 0);
    CHECK(config_store_used(&tiny) <= sizeof(small));

    /* Everything that was accepted is still readable. */
    for (unsigned i = 0; i < added; i++) {
        char key[16];
        snprintf(key, sizeof(key), "k%u", i);
        char value[16];
        if (config_get_string(&tiny, key, value, sizeof(value), "") != CONFIG_OK ||
            strcmp(value, "value") != 0) {
            printf("    key %s was lost\n", key);
            CHECK(false);
            return;
        }
    }
}

TEST(growing_a_value_beyond_the_space_left_is_refused_without_damage)
{
    uint8_t small[32];
    config_store_t tiny;
    config_store_init(&tiny, small, sizeof(small));

    config_set_string(&tiny, "a", "one");
    config_set_string(&tiny, "b", "two");

    char large[64];
    memset(large, 'x', sizeof(large) - 1);
    large[sizeof(large) - 1] = '\0';

    CHECK_EQ_INT(config_set_string(&tiny, "a", large), CONFIG_ERR_FULL);

    /* Both values survive the refusal. */
    char value[16];
    CHECK_EQ_INT(config_get_string(&tiny, "a", value, sizeof(value), ""), CONFIG_OK);
    CHECK_EQ_STR(value, "one");
    CHECK_EQ_INT(config_get_string(&tiny, "b", value, sizeof(value), ""), CONFIG_OK);
    CHECK_EQ_STR(value, "two");
}

TEST(a_value_filling_the_buffer_exactly_is_accepted)
{
    /* Off-by-one at the boundary would either waste a byte or overflow. */
    uint8_t exact[16];
    config_store_t tiny;
    config_store_init(&tiny, exact, sizeof(exact));

    /* key "k" (1) + lengths (2) = 3, so a 13-byte value fills 16 exactly. */
    uint8_t value[13];
    memset(value, 0x5A, sizeof(value));

    CHECK_EQ_INT(config_set(&tiny, "k", value, sizeof(value)), CONFIG_OK);
    CHECK_EQ_INT(config_store_used(&tiny), sizeof(exact));
    CHECK_EQ_INT(config_store_free(&tiny), 0);

    /* One more byte would not have fitted. */
    config_store_init(&tiny, exact, sizeof(exact));
    uint8_t bigger[14];
    memset(bigger, 0x5A, sizeof(bigger));
    CHECK_EQ_INT(config_set(&tiny, "k", bigger, sizeof(bigger)), CONFIG_ERR_FULL);
}

TEST(an_over_long_key_is_reported_as_such)
{
    setup();
    char key[CONFIG_MAX_KEY_LENGTH + 8];
    memset(key, 'k', sizeof(key) - 1);
    key[sizeof(key) - 1] = '\0';

    CHECK_EQ_INT(config_set_string(&store, key, "x"), CONFIG_ERR_KEY_TOO_LONG);

    /* Exactly the limit is fine. */
    key[CONFIG_MAX_KEY_LENGTH] = '\0';
    CHECK_EQ_INT(config_set_string(&store, key, "x"), CONFIG_OK);
}

TEST(a_null_or_empty_key_is_rejected)
{
    setup();
    CHECK_EQ_INT(config_set_string(&store, NULL, "x"), CONFIG_ERR_INVALID_ARG);
    CHECK_EQ_INT(config_set_string(&store, "", "x"), CONFIG_ERR_INVALID_ARG);
}

TEST(a_value_that_does_not_fit_the_callers_buffer_reports_its_length)
{
    /* So the caller can size a buffer and retry rather than guess. */
    setup();
    config_set_string(&store, "long", "twenty-two characters!");

    uint8_t small[4];
    uint8_t length = 0;
    CHECK_EQ_INT(config_get(&store, "long", small, sizeof(small), &length),
                 CONFIG_ERR_TOO_SMALL);
    CHECK_EQ_INT(length, 22);
}

TEST(a_string_too_long_for_the_buffer_falls_back_rather_than_truncating)
{
    /* Half a WiFi password is worse than the default, because it looks like a
       value. */
    setup();
    config_set_string(&store, "pass", "a-very-long-password-indeed");

    char small[8];
    CHECK_EQ_INT(config_get_string(&store, "pass", small, sizeof(small), "default"),
                 CONFIG_ERR_TOO_SMALL);
    CHECK_EQ_STR(small, "default");
}

TEST(a_value_stored_at_the_wrong_width_is_reported_not_reassembled)
{
    /* Reading a string as a number should say so rather than return whatever
       the first four bytes happen to be. */
    setup();

    /* Longer than four bytes and shorter than four bytes are the same mistake,
       so both must report it the same way. */
    config_set_string(&store, "long", "hello");
    config_set_string(&store, "short", "ab");

    uint32_t value = 0;
    CHECK_EQ_INT(config_get_u32(&store, "long", &value, 7), CONFIG_ERR_CORRUPT);
    CHECK_EQ_U32(value, 7u);   /* the fallback stands */
    CHECK_EQ_INT(config_get_u32(&store, "short", &value, 7), CONFIG_ERR_CORRUPT);
}

/* ---------------------------------------------------------------------------
 * Adopting records from flash
 * -------------------------------------------------------------------------*/

TEST(a_saved_image_loads_back)
{
    setup();
    config_set_string(&store, "a", "one");
    config_set_u32(&store, "n", 12345);
    config_set_string(&store, "b", "two");

    /* What a save writes and a load reads back. */
    uint8_t saved[CAPACITY];
    const size_t length = config_store_used(&store);
    memcpy(saved, buffer, length);

    config_store_t loaded;
    CHECK_EQ_INT(config_store_load(&loaded, saved, sizeof(saved), length), CONFIG_OK);

    char value[32];
    CHECK_EQ_INT(config_get_string(&loaded, "a", value, sizeof(value), ""), CONFIG_OK);
    CHECK_EQ_STR(value, "one");

    uint32_t number = 0;
    CHECK_EQ_INT(config_get_u32(&loaded, "n", &number, 0), CONFIG_OK);
    CHECK_EQ_U32(number, 12345u);
    CHECK_EQ_INT(config_count(&loaded), 3);
}

TEST(a_truncated_image_is_rejected_rather_than_partly_adopted)
{
    /*
     * Exactly what an interrupted save leaves. Adopting the records that do
     * parse would give a configuration that is half old and half new, which is
     * worse than none — so every prefix that cuts a record short is refused.
     */
    setup();
    config_set_string(&store, "aaa", "one");
    config_set_string(&store, "bbb", "two");
    config_set_string(&store, "ccc", "three");

    const size_t length = config_store_used(&store);
    uint8_t saved[CAPACITY];
    memcpy(saved, buffer, length);

    unsigned rejected = 0;
    for (size_t cut = 1; cut < length; cut++) {
        config_store_t loaded;
        if (config_store_load(&loaded, saved, sizeof(saved), cut) != CONFIG_OK) {
            rejected++;
        }
    }

    /* Some prefixes land exactly on a record boundary and are legitimately
       valid; the rest must be refused. */
    CHECK(rejected > 0);

    /* And the full image is still accepted. */
    config_store_t loaded;
    CHECK_EQ_INT(config_store_load(&loaded, saved, sizeof(saved), length), CONFIG_OK);
}

TEST(an_image_with_a_nonsense_record_is_rejected)
{
    uint8_t rubbish[32];
    memset(rubbish, 0xFF, sizeof(rubbish));   /* as erased flash reads */

    config_store_t loaded;
    CHECK_EQ_INT(config_store_load(&loaded, rubbish, sizeof(rubbish), sizeof(rubbish)),
                 CONFIG_ERR_CORRUPT);

    memset(rubbish, 0x00, sizeof(rubbish));   /* a zero key length */
    CHECK_EQ_INT(config_store_load(&loaded, rubbish, sizeof(rubbish), sizeof(rubbish)),
                 CONFIG_ERR_CORRUPT);
}

TEST(an_empty_image_loads_as_an_empty_store)
{
    uint8_t empty[32];
    config_store_t loaded;

    CHECK_EQ_INT(config_store_load(&loaded, empty, sizeof(empty), 0), CONFIG_OK);
    CHECK_EQ_INT(config_count(&loaded), 0);
    CHECK_EQ_INT(config_store_used(&loaded), 0);
}

/* ---------------------------------------------------------------------------
 * Enumeration
 * -------------------------------------------------------------------------*/

TEST(keys_can_be_listed)
{
    setup();
    config_set_string(&store, "alpha", "1");
    config_set_string(&store, "beta", "2");
    config_set_string(&store, "gamma", "3");

    char key[CONFIG_MAX_KEY_LENGTH + 1];
    CHECK(config_key_at(&store, 0, key, sizeof(key)));
    CHECK_EQ_STR(key, "alpha");
    CHECK(config_key_at(&store, 1, key, sizeof(key)));
    CHECK_EQ_STR(key, "beta");
    CHECK(config_key_at(&store, 2, key, sizeof(key)));
    CHECK_EQ_STR(key, "gamma");
    CHECK(!config_key_at(&store, 3, key, sizeof(key)));
}

TEST(clear_empties_the_store_but_leaves_it_usable)
{
    setup();
    config_set_string(&store, "a", "one");
    config_clear(&store);

    CHECK_EQ_INT(config_count(&store), 0);
    CHECK(!config_has(&store, "a"));
    CHECK_EQ_INT(config_set_string(&store, "b", "two"), CONFIG_OK);
    CHECK_EQ_STR(get_string("b"), "two");
}

TEST_MAIN(
    RUN(a_value_comes_back);
    RUN(an_absent_key_yields_the_fallback);
    RUN(several_keys_coexist);
    RUN(a_thirty_two_bit_value_round_trips);
    RUN(a_zero_length_value_is_not_the_same_as_absent);

    RUN(overwriting_a_value_with_the_same_length_keeps_the_neighbours);
    RUN(growing_a_value_in_the_middle_keeps_the_neighbours);
    RUN(shrinking_a_value_in_the_middle_keeps_the_neighbours);
    RUN(removing_a_key_closes_the_gap);
    RUN(removing_the_first_and_last_keys_works);
    RUN(removing_every_key_empties_the_store);
    RUN(removing_an_absent_key_changes_nothing);
    RUN(many_rewrites_leave_the_store_consistent);

    RUN(a_full_store_refuses_rather_than_overflows);
    RUN(growing_a_value_beyond_the_space_left_is_refused_without_damage);
    RUN(a_value_filling_the_buffer_exactly_is_accepted);
    RUN(an_over_long_key_is_reported_as_such);
    RUN(a_null_or_empty_key_is_rejected);
    RUN(a_value_that_does_not_fit_the_callers_buffer_reports_its_length);
    RUN(a_string_too_long_for_the_buffer_falls_back_rather_than_truncating);
    RUN(a_value_stored_at_the_wrong_width_is_reported_not_reassembled);

    RUN(a_saved_image_loads_back);
    RUN(a_truncated_image_is_rejected_rather_than_partly_adopted);
    RUN(an_image_with_a_nonsense_record_is_rejected);
    RUN(an_empty_image_loads_as_an_empty_store);

    RUN(keys_can_be_listed);
    RUN(clear_empties_the_store_but_leaves_it_usable);
)
