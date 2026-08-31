/*
 * wifi_policy - when to retry a connection, and whether the credentials are
 * usable at all.
 *
 * The parts of WiFi management that are decisions rather than radio: how long
 * to wait before trying again, when to give up, and whether an SSID and
 * passphrase are within what WPA2 allows. No Pico SDK dependency, so all of it
 * is unit-tested on the host — which matters here because the alternative is
 * testing retry behaviour by unplugging an access point and waiting.
 */

#ifndef PICO_FRAMEWORK_WIFI_POLICY_H
#define PICO_FRAMEWORK_WIFI_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 802.11 allows 1 to 32 octets of SSID. */
#define WIFI_SSID_MIN_LENGTH 1u
#define WIFI_SSID_MAX_LENGTH 32u

/* WPA2 personal passphrases are 8 to 63 printable characters. A 64-character
   value is a hex PSK rather than a passphrase and is not accepted here. */
#define WIFI_PASSWORD_MIN_LENGTH 8u
#define WIFI_PASSWORD_MAX_LENGTH 63u

typedef enum {
    WIFI_CREDENTIALS_OK = 0,
    WIFI_CREDENTIALS_NO_SSID,
    WIFI_CREDENTIALS_SSID_TOO_LONG,
    WIFI_CREDENTIALS_PASSWORD_TOO_SHORT,
    WIFI_CREDENTIALS_PASSWORD_TOO_LONG,
} wifi_credentials_result_t;

/*
 * Check an SSID and passphrase before handing them to the radio.
 *
 * `password` may be NULL or empty for an open network. Checking here means a
 * typo is reported as a bad passphrase rather than as a network that will not
 * associate, which are very different things to debug.
 */
wifi_credentials_result_t wifi_check_credentials(const char *ssid, const char *password);

const char *wifi_credentials_result_name(wifi_credentials_result_t result);

/* ---------------------------------------------------------------------------
 * Retrying
 *
 * Exponential backoff with a ceiling. A robot that loses its access point
 * should keep trying, but retrying every 100 ms forever floods the air and
 * keeps the radio busy; doubling up to a cap costs almost nothing and still
 * reconnects promptly when the network comes back.
 * -------------------------------------------------------------------------*/

typedef struct {
    /* Wait before the second attempt. Doubles from there. */
    uint32_t first_delay_ms;

    /* Ceiling on the wait, so it never becomes unresponsive. */
    uint32_t max_delay_ms;

    /* Attempts before giving up. 0 means never give up, which is what a robot
       usually wants. */
    uint32_t max_attempts;
} wifi_retry_config_t;

#define WIFI_RETRY_DEFAULT_FIRST_DELAY_MS 500u
#define WIFI_RETRY_DEFAULT_MAX_DELAY_MS 30000u

typedef struct {
    wifi_retry_config_t config;
    uint32_t attempts;
    uint32_t next_attempt_ms;
    bool waiting;
} wifi_retry_t;

/* Zero fields in `config` take the defaults above. */
void wifi_retry_init(wifi_retry_t *retry, const wifi_retry_config_t *config);

/* Forget the history, as after a successful connection. */
void wifi_retry_reset(wifi_retry_t *retry);

/*
 * Record a failed attempt and return when the next may start.
 *
 * The delay is `first_delay_ms` doubled once per previous failure, capped at
 * `max_delay_ms`.
 */
uint32_t wifi_retry_fail(wifi_retry_t *retry, uint32_t now_ms);

/*
 * Is it time to try again?
 *
 * The comparison is a signed difference, so it stays correct when the
 * millisecond counter wraps — which it does every 49.7 days. Comparing
 * directly would, exactly once per wrap, decide that the next attempt is 49
 * days away and leave a robot offline until it was power-cycled.
 */
bool wifi_retry_due(const wifi_retry_t *retry, uint32_t now_ms);

/* Has the attempt budget run out? Always false when max_attempts is 0. */
bool wifi_retry_exhausted(const wifi_retry_t *retry);

static inline uint32_t wifi_retry_attempts(const wifi_retry_t *retry)
{
    return retry->attempts;
}

/* The delay a given attempt number would wait. Exposed for tests and for a
   status command that wants to show the schedule. */
uint32_t wifi_retry_delay_for_attempt(const wifi_retry_config_t *config,
                                      uint32_t attempt);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WIFI_POLICY_H */
