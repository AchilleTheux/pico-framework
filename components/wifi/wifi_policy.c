#include <string.h>

#include "wifi_policy.h"

wifi_credentials_result_t wifi_check_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL) {
        return WIFI_CREDENTIALS_NO_SSID;
    }

    const size_t ssid_length = strlen(ssid);
    if (ssid_length < WIFI_SSID_MIN_LENGTH) {
        return WIFI_CREDENTIALS_NO_SSID;
    }
    if (ssid_length > WIFI_SSID_MAX_LENGTH) {
        return WIFI_CREDENTIALS_SSID_TOO_LONG;
    }

    /* An absent or empty passphrase means an open network, which is legal. */
    if (password == NULL || password[0] == '\0') {
        return WIFI_CREDENTIALS_OK;
    }

    const size_t password_length = strlen(password);
    if (password_length < WIFI_PASSWORD_MIN_LENGTH) {
        return WIFI_CREDENTIALS_PASSWORD_TOO_SHORT;
    }
    if (password_length > WIFI_PASSWORD_MAX_LENGTH) {
        return WIFI_CREDENTIALS_PASSWORD_TOO_LONG;
    }

    return WIFI_CREDENTIALS_OK;
}

const char *wifi_credentials_result_name(wifi_credentials_result_t result)
{
    switch (result) {
        case WIFI_CREDENTIALS_OK:                  return "ok";
        case WIFI_CREDENTIALS_NO_SSID:             return "no ssid";
        case WIFI_CREDENTIALS_SSID_TOO_LONG:       return "ssid longer than 32 characters";
        case WIFI_CREDENTIALS_PASSWORD_TOO_SHORT:  return "passphrase shorter than 8 characters";
        case WIFI_CREDENTIALS_PASSWORD_TOO_LONG:   return "passphrase longer than 63 characters";
        default:                                   return "unknown";
    }
}

/* ---------------------------------------------------------------------------
 * Retrying
 * -------------------------------------------------------------------------*/

uint32_t wifi_retry_delay_for_attempt(const wifi_retry_config_t *config,
                                      uint32_t attempt)
{
    if (config == NULL || attempt == 0) {
        return 0;
    }

    const uint32_t first = (config->first_delay_ms != 0)
        ? config->first_delay_ms
        : WIFI_RETRY_DEFAULT_FIRST_DELAY_MS;
    const uint32_t ceiling = (config->max_delay_ms != 0)
        ? config->max_delay_ms
        : WIFI_RETRY_DEFAULT_MAX_DELAY_MS;

    uint32_t delay = first;
    for (uint32_t i = 1; i < attempt; i++) {
        /* Doubled by addition rather than a shift, so a long-running failure
           cannot overflow the delay round to something tiny. */
        if (delay >= ceiling || delay > (0xFFFFFFFFu - delay)) {
            return ceiling;
        }
        delay += delay;
    }

    return (delay < ceiling) ? delay : ceiling;
}

void wifi_retry_init(wifi_retry_t *retry, const wifi_retry_config_t *config)
{
    if (retry == NULL) {
        return;
    }

    memset(retry, 0, sizeof(*retry));
    if (config != NULL) {
        retry->config = *config;
    }
    if (retry->config.first_delay_ms == 0) {
        retry->config.first_delay_ms = WIFI_RETRY_DEFAULT_FIRST_DELAY_MS;
    }
    if (retry->config.max_delay_ms == 0) {
        retry->config.max_delay_ms = WIFI_RETRY_DEFAULT_MAX_DELAY_MS;
    }
}

void wifi_retry_reset(wifi_retry_t *retry)
{
    if (retry != NULL) {
        retry->attempts = 0;
        retry->next_attempt_ms = 0;
        retry->waiting = false;
    }
}

uint32_t wifi_retry_fail(wifi_retry_t *retry, uint32_t now_ms)
{
    if (retry == NULL) {
        return 0;
    }

    retry->attempts++;
    const uint32_t delay = wifi_retry_delay_for_attempt(&retry->config, retry->attempts);

    retry->next_attempt_ms = now_ms + delay;
    retry->waiting = true;
    return retry->next_attempt_ms;
}

bool wifi_retry_due(const wifi_retry_t *retry, uint32_t now_ms)
{
    if (retry == NULL) {
        return false;
    }
    if (!retry->waiting) {
        return true;   /* nothing has failed yet, so go ahead */
    }

    /*
     * Signed difference, so a wrapped counter still compares correctly. The
     * millisecond counter wraps every 49.7 days; a direct comparison would,
     * once per wrap, decide the next attempt was 49 days away and leave a
     * robot offline until someone power-cycled it.
     */
    return (int32_t)(now_ms - retry->next_attempt_ms) >= 0;
}

bool wifi_retry_exhausted(const wifi_retry_t *retry)
{
    if (retry == NULL) {
        return true;
    }
    if (retry->config.max_attempts == 0) {
        return false;   /* never give up */
    }
    return retry->attempts >= retry->config.max_attempts;
}
