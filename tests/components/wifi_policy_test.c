/*
 * Host-side tests for the WiFi retry policy and credential checks.
 *
 * These are the parts of WiFi management that are decisions rather than radio,
 * and they matter because the alternative way to test retry behaviour is to
 * unplug an access point and wait — for up to 49 days, in the case of the
 * wraparound below.
 */

#include <string.h>

#include "test.h"

#include "wifi_policy.h"

/* ---------------------------------------------------------------------------
 * Credentials
 * -------------------------------------------------------------------------*/

TEST(reasonable_credentials_are_accepted)
{
    CHECK_EQ_INT(wifi_check_credentials("robot-net", "a-good-password"),
                 WIFI_CREDENTIALS_OK);
}

TEST(an_open_network_needs_no_passphrase)
{
    CHECK_EQ_INT(wifi_check_credentials("open-net", NULL), WIFI_CREDENTIALS_OK);
    CHECK_EQ_INT(wifi_check_credentials("open-net", ""), WIFI_CREDENTIALS_OK);
}

TEST(a_missing_ssid_is_rejected)
{
    CHECK_EQ_INT(wifi_check_credentials(NULL, "password"), WIFI_CREDENTIALS_NO_SSID);
    CHECK_EQ_INT(wifi_check_credentials("", "password"), WIFI_CREDENTIALS_NO_SSID);
}

TEST(the_ssid_length_limit_is_thirty_two)
{
    char ssid[WIFI_SSID_MAX_LENGTH + 2];
    memset(ssid, 'a', sizeof(ssid) - 1);
    ssid[sizeof(ssid) - 1] = '\0';
    CHECK_EQ_INT(wifi_check_credentials(ssid, "password"),
                 WIFI_CREDENTIALS_SSID_TOO_LONG);

    /* Exactly 32 is legal. */
    ssid[WIFI_SSID_MAX_LENGTH] = '\0';
    CHECK_EQ_INT(wifi_check_credentials(ssid, "password"), WIFI_CREDENTIALS_OK);
}

TEST(the_passphrase_limits_are_eight_and_sixty_three)
{
    /*
     * WPA2 personal. Reporting a short passphrase here means a typo shows up as
     * a bad passphrase rather than as a network that will not associate, which
     * are very different things to chase.
     */
    CHECK_EQ_INT(wifi_check_credentials("net", "short"),
                 WIFI_CREDENTIALS_PASSWORD_TOO_SHORT);
    CHECK_EQ_INT(wifi_check_credentials("net", "12345678"), WIFI_CREDENTIALS_OK);

    char password[WIFI_PASSWORD_MAX_LENGTH + 3];
    memset(password, 'p', sizeof(password) - 1);
    password[sizeof(password) - 1] = '\0';
    CHECK_EQ_INT(wifi_check_credentials("net", password),
                 WIFI_CREDENTIALS_PASSWORD_TOO_LONG);

    password[WIFI_PASSWORD_MAX_LENGTH] = '\0';
    CHECK_EQ_INT(wifi_check_credentials("net", password), WIFI_CREDENTIALS_OK);
}

TEST(a_sixty_four_character_value_is_refused)
{
    /*
     * 64 characters is a hex PSK, not a passphrase. Passing one where a
     * passphrase is expected associates with nothing and gives no clue why.
     */
    char psk[65];
    memset(psk, 'a', 64);
    psk[64] = '\0';
    CHECK_EQ_INT(wifi_check_credentials("net", psk), WIFI_CREDENTIALS_PASSWORD_TOO_LONG);
}

/* ---------------------------------------------------------------------------
 * Backoff
 * -------------------------------------------------------------------------*/

TEST(the_delay_doubles_and_then_stops)
{
    const wifi_retry_config_t config = {
        .first_delay_ms = 500, .max_delay_ms = 8000, .max_attempts = 0,
    };

    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 1), 500u);
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 2), 1000u);
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 3), 2000u);
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 4), 4000u);
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 5), 8000u);
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 6), 8000u);  /* capped */
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&config, 100), 8000u);
}

TEST(the_delay_never_overflows_back_to_something_tiny)
{
    /*
     * The failure worth guarding: doubling by a shift would, after enough
     * failures, roll over to a very small delay and turn polite backoff into a
     * flood. A robot left overnight against a dead access point would reach it.
     */
    const wifi_retry_config_t config = {
        .first_delay_ms = 1000, .max_delay_ms = 60000, .max_attempts = 0,
    };

    for (uint32_t attempt = 1; attempt < 200; attempt++) {
        const uint32_t delay = wifi_retry_delay_for_attempt(&config, attempt);
        if (delay < 1000u || delay > 60000u) {
            printf("    attempt %u gave %u ms\n", attempt, delay);
            CHECK(false);
            return;
        }
    }
}

TEST(zero_fields_take_the_defaults)
{
    const wifi_retry_config_t empty = { 0 };

    CHECK_EQ_U32(wifi_retry_delay_for_attempt(&empty, 1),
                 WIFI_RETRY_DEFAULT_FIRST_DELAY_MS);

    wifi_retry_t retry;
    wifi_retry_init(&retry, &empty);
    CHECK_EQ_U32(retry.config.first_delay_ms, WIFI_RETRY_DEFAULT_FIRST_DELAY_MS);
    CHECK_EQ_U32(retry.config.max_delay_ms, WIFI_RETRY_DEFAULT_MAX_DELAY_MS);

    wifi_retry_init(&retry, NULL);
    CHECK_EQ_U32(retry.config.first_delay_ms, WIFI_RETRY_DEFAULT_FIRST_DELAY_MS);
}

TEST(a_fresh_policy_lets_the_first_attempt_through)
{
    wifi_retry_t retry;
    wifi_retry_init(&retry, NULL);

    CHECK(wifi_retry_due(&retry, 0));
    CHECK(wifi_retry_due(&retry, 123456));
    CHECK(!wifi_retry_exhausted(&retry));
}

TEST(a_failure_holds_the_next_attempt_off)
{
    const wifi_retry_config_t config = {
        .first_delay_ms = 500, .max_delay_ms = 8000, .max_attempts = 0,
    };
    wifi_retry_t retry;
    wifi_retry_init(&retry, &config);

    const uint32_t next = wifi_retry_fail(&retry, 1000);
    CHECK_EQ_U32(next, 1500u);

    CHECK(!wifi_retry_due(&retry, 1000));
    CHECK(!wifi_retry_due(&retry, 1499));
    CHECK(wifi_retry_due(&retry, 1500));
    CHECK(wifi_retry_due(&retry, 5000));
}

TEST(successive_failures_wait_longer)
{
    const wifi_retry_config_t config = {
        .first_delay_ms = 100, .max_delay_ms = 1000, .max_attempts = 0,
    };
    wifi_retry_t retry;
    wifi_retry_init(&retry, &config);

    uint32_t now = 0;
    uint32_t previous_delay = 0;

    for (unsigned i = 0; i < 6; i++) {
        const uint32_t next = wifi_retry_fail(&retry, now);
        const uint32_t delay = next - now;

        if (i > 0 && delay < previous_delay) {
            printf("    attempt %u waited %u, less than the previous %u\n", i + 1,
                   delay, previous_delay);
            CHECK(false);
            return;
        }
        previous_delay = delay;
        now = next;
    }

    CHECK_EQ_U32(previous_delay, 1000u);   /* reached the ceiling */
}

TEST(success_forgets_the_history)
{
    const wifi_retry_config_t config = {
        .first_delay_ms = 500, .max_delay_ms = 8000, .max_attempts = 0,
    };
    wifi_retry_t retry;
    wifi_retry_init(&retry, &config);

    wifi_retry_fail(&retry, 0);
    wifi_retry_fail(&retry, 1000);
    wifi_retry_fail(&retry, 5000);
    CHECK(wifi_retry_attempts(&retry) == 3);

    /* After a successful connection the next outage should retry promptly
       rather than inheriting a long delay from an old one. */
    wifi_retry_reset(&retry);
    CHECK_EQ_U32(wifi_retry_attempts(&retry), 0u);
    CHECK(wifi_retry_due(&retry, 5000));
    CHECK_EQ_U32(wifi_retry_fail(&retry, 5000) - 5000u, 500u);
}

TEST(the_attempt_budget_can_run_out)
{
    const wifi_retry_config_t config = {
        .first_delay_ms = 100, .max_delay_ms = 1000, .max_attempts = 3,
    };
    wifi_retry_t retry;
    wifi_retry_init(&retry, &config);

    CHECK(!wifi_retry_exhausted(&retry));
    wifi_retry_fail(&retry, 0);
    CHECK(!wifi_retry_exhausted(&retry));
    wifi_retry_fail(&retry, 1000);
    CHECK(!wifi_retry_exhausted(&retry));
    wifi_retry_fail(&retry, 2000);
    CHECK(wifi_retry_exhausted(&retry));
}

TEST(a_zero_budget_never_runs_out)
{
    /* What a robot wants: keep trying, however long the access point is away. */
    const wifi_retry_config_t config = {
        .first_delay_ms = 100, .max_delay_ms = 1000, .max_attempts = 0,
    };
    wifi_retry_t retry;
    wifi_retry_init(&retry, &config);

    for (unsigned i = 0; i < 1000; i++) {
        wifi_retry_fail(&retry, i * 1000u);
        if (wifi_retry_exhausted(&retry)) {
            printf("    gave up after %u attempts\n", i + 1);
            CHECK(false);
            return;
        }
    }
}

/* ---------------------------------------------------------------------------
 * The wraparound
 * -------------------------------------------------------------------------*/

TEST(a_retry_scheduled_across_the_counter_wrap_still_becomes_due)
{
    /*
     * The millisecond counter wraps every 49.7 days. A direct comparison would,
     * exactly once per wrap, decide the next attempt was 49 days away and leave
     * a robot offline until someone power-cycled it. This is the reason the
     * comparison is a signed difference, and it is not something anyone would
     * find by testing against a real access point.
     */
    wifi_retry_t retry;
    const wifi_retry_config_t config = {
        .first_delay_ms = 1000, .max_delay_ms = 1000, .max_attempts = 0,
    };
    wifi_retry_init(&retry, &config);

    /* Fail 500 ms before the counter wraps, so the next attempt is 500 ms
       after it. */
    const uint32_t before_wrap = 0xFFFFFFFFu - 500u;
    wifi_retry_fail(&retry, before_wrap);

    CHECK(!wifi_retry_due(&retry, before_wrap));
    CHECK(!wifi_retry_due(&retry, 0xFFFFFFFFu));   /* the instant of the wrap */
    CHECK(wifi_retry_due(&retry, 500u));           /* just past it: due */
    CHECK(wifi_retry_due(&retry, 1000u));
}

TEST(the_wrap_handling_does_not_make_everything_due)
{
    /* The other way it could be got wrong: a signed comparison that reports
       due immediately would remove the backoff altogether. */
    wifi_retry_t retry;
    const wifi_retry_config_t config = {
        .first_delay_ms = 10000, .max_delay_ms = 10000, .max_attempts = 0,
    };
    wifi_retry_init(&retry, &config);

    wifi_retry_fail(&retry, 1000);
    for (uint32_t now = 1000; now < 11000; now += 500) {
        if (wifi_retry_due(&retry, now)) {
            printf("    due at %u, only %u after the failure\n", now, now - 1000u);
            CHECK(false);
            return;
        }
    }
    CHECK(wifi_retry_due(&retry, 11000));
}

TEST(null_arguments_are_survivable)
{
    CHECK_EQ_U32(wifi_retry_delay_for_attempt(NULL, 1), 0u);
    CHECK_EQ_U32(wifi_retry_fail(NULL, 0), 0u);
    CHECK(!wifi_retry_due(NULL, 0));
    CHECK(wifi_retry_exhausted(NULL));
    wifi_retry_init(NULL, NULL);
    wifi_retry_reset(NULL);
}

TEST_MAIN(
    RUN(reasonable_credentials_are_accepted);
    RUN(an_open_network_needs_no_passphrase);
    RUN(a_missing_ssid_is_rejected);
    RUN(the_ssid_length_limit_is_thirty_two);
    RUN(the_passphrase_limits_are_eight_and_sixty_three);
    RUN(a_sixty_four_character_value_is_refused);

    RUN(the_delay_doubles_and_then_stops);
    RUN(the_delay_never_overflows_back_to_something_tiny);
    RUN(zero_fields_take_the_defaults);
    RUN(a_fresh_policy_lets_the_first_attempt_through);
    RUN(a_failure_holds_the_next_attempt_off);
    RUN(successive_failures_wait_longer);
    RUN(success_forgets_the_history);
    RUN(the_attempt_budget_can_run_out);
    RUN(a_zero_budget_never_runs_out);

    RUN(a_retry_scheduled_across_the_counter_wrap_still_becomes_due);
    RUN(the_wrap_handling_does_not_make_everything_due);
    RUN(null_arguments_are_survivable);
)
