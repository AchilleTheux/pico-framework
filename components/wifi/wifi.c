#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"

#include "wifi.h"

#if WIFI_SUPPORTED
#include "pico/cyw43_arch.h"
#endif

#define NO_ADDRESS "0.0.0.0"

static void set_no_address(wifi_t *wifi)
{
    snprintf(wifi->address, sizeof(wifi->address), "%s", NO_ADDRESS);
}

const char *wifi_state_name(wifi_state_t state)
{
    switch (state) {
        case WIFI_STATE_IDLE:       return "idle";
        case WIFI_STATE_CONNECTING: return "connecting";
        case WIFI_STATE_CONNECTED:  return "connected";
        case WIFI_STATE_WAITING:    return "waiting to retry";
        case WIFI_STATE_GAVE_UP:    return "gave up";
        default:                    return "unknown";
    }
}

const char *wifi_result_name(wifi_result_t result)
{
    switch (result) {
        case WIFI_OK:                  return "ok";
        case WIFI_ERR_INVALID_ARG:     return "invalid argument";
        case WIFI_ERR_UNSUPPORTED:     return "no radio on this board";
        case WIFI_ERR_RADIO:           return "the radio would not start";
        case WIFI_ERR_CREDENTIALS:     return "credentials WPA2 cannot accept";
        case WIFI_ERR_NOT_CONNECTED:   return "not connected";
        case WIFI_ERR_GAVE_UP:         return "retry budget exhausted";
        default:                       return "unknown";
    }
}

const char *wifi_address(const wifi_t *wifi)
{
    if (wifi == NULL || !wifi->initialised || wifi->address[0] == '\0') {
        return NO_ADDRESS;
    }
    return wifi->address;
}

#if !WIFI_SUPPORTED

/*
 * No radio on this board.
 *
 * The component still builds, so it need not be conditionally registered and
 * the CI matrix stays uniform. Every call reports WIFI_ERR_UNSUPPORTED rather
 * than failing to link, and WIFI_SUPPORTED lets a caller find out at compile
 * time instead.
 */

wifi_result_t wifi_init(wifi_t *wifi)
{
    if (wifi != NULL) {
        memset(wifi, 0, sizeof(*wifi));
        set_no_address(wifi);
    }
    return WIFI_ERR_UNSUPPORTED;
}

void wifi_deinit(wifi_t *wifi) { (void)wifi; }

wifi_result_t wifi_connect(wifi_t *wifi, const wifi_config_t *config)
{
    (void)wifi;
    (void)config;
    return WIFI_ERR_UNSUPPORTED;
}

wifi_result_t wifi_disconnect(wifi_t *wifi)
{
    (void)wifi;
    return WIFI_ERR_UNSUPPORTED;
}

void wifi_poll(wifi_t *wifi) { (void)wifi; }

int32_t wifi_rssi(wifi_t *wifi)
{
    (void)wifi;
    return 0;
}

#else /* WIFI_SUPPORTED */

static uint32_t now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

wifi_result_t wifi_init(wifi_t *wifi)
{
    if (wifi == NULL) {
        return WIFI_ERR_INVALID_ARG;
    }

    memset(wifi, 0, sizeof(*wifi));
    set_no_address(wifi);

    /*
     * Uploads the CYW43's firmware from flash, which takes a moment. The only
     * blocking call in the component, and unavoidable — the radio cannot be
     * brought up incrementally.
     */
    if (cyw43_arch_init() != 0) {
        return WIFI_ERR_RADIO;
    }

    cyw43_arch_enable_sta_mode();

    wifi->radio_started = true;
    wifi->initialised = true;
    wifi->state = WIFI_STATE_IDLE;
    return WIFI_OK;
}

void wifi_deinit(wifi_t *wifi)
{
    if (wifi == NULL || !wifi->initialised) {
        return;
    }
    if (wifi->radio_started) {
        cyw43_arch_deinit();
        wifi->radio_started = false;
    }
    wifi->initialised = false;
    wifi->state = WIFI_STATE_IDLE;
}

/* Ask the radio to begin associating, without waiting for the outcome. */
static wifi_result_t begin_attempt(wifi_t *wifi)
{
    const char *password = wifi->config.password;
    if (password != NULL && password[0] == '\0') {
        password = NULL;   /* an open network */
    }

    const uint32_t auth = (password != NULL) ? CYW43_AUTH_WPA2_AES_PSK
                                             : CYW43_AUTH_OPEN;

    /*
     * The asynchronous form. The blocking one would sit here for seconds while
     * the radio scans and associates, which is exactly what this component
     * exists to avoid.
     */
    if (cyw43_arch_wifi_connect_async(wifi->config.ssid, password, auth) != 0) {
        return WIFI_ERR_RADIO;
    }

    wifi->attempt_started_ms = now_ms();
    wifi->state = WIFI_STATE_CONNECTING;
    return WIFI_OK;
}

wifi_result_t wifi_connect(wifi_t *wifi, const wifi_config_t *config)
{
    if (wifi == NULL || !wifi->initialised || config == NULL) {
        return WIFI_ERR_INVALID_ARG;
    }

    /* Checked before the radio is asked, so a passphrase WPA2 could never
       accept is reported as such rather than as a network that will not
       associate. */
    if (wifi_check_credentials(config->ssid, config->password) != WIFI_CREDENTIALS_OK) {
        return WIFI_ERR_CREDENTIALS;
    }

    wifi->config = *config;
    if (wifi->config.attempt_timeout_ms == 0) {
        wifi->config.attempt_timeout_ms = WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS;
    }

    if (config->hostname != NULL) {
        netif_set_hostname(netif_default, config->hostname);
    }

    wifi_retry_init(&wifi->retry, &wifi->config.retry);
    set_no_address(wifi);

    return begin_attempt(wifi);
}

wifi_result_t wifi_disconnect(wifi_t *wifi)
{
    if (wifi == NULL || !wifi->initialised) {
        return WIFI_ERR_INVALID_ARG;
    }

    cyw43_wifi_leave(&cyw43_state, CYW43_ITF_STA);
    wifi_retry_reset(&wifi->retry);
    set_no_address(wifi);
    wifi->state = WIFI_STATE_IDLE;
    return WIFI_OK;
}

/* Record the address lwIP was given, so a status line has something to show. */
static void capture_address(wifi_t *wifi)
{
    const ip4_addr_t *address = netif_ip4_addr(netif_default);

    if (address == NULL || ip4_addr_get_u32(address) == 0) {
        set_no_address(wifi);
        return;
    }
    snprintf(wifi->address, sizeof(wifi->address), "%s", ip4addr_ntoa(address));
}

static void handle_failure(wifi_t *wifi)
{
    set_no_address(wifi);

    if (wifi_retry_exhausted(&wifi->retry)) {
        wifi->state = WIFI_STATE_GAVE_UP;
        return;
    }

    wifi_retry_fail(&wifi->retry, now_ms());
    wifi->state = WIFI_STATE_WAITING;
}

void wifi_poll(wifi_t *wifi)
{
    if (wifi == NULL || !wifi->initialised) {
        return;
    }

    /*
     * lwIP and the driver get their turn first. In poll mode nothing happens
     * unless this is called, so a caller that forgets it sees a link that never
     * associates and no error to explain why.
     */
    cyw43_arch_poll();

    const int link = cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);

    switch (wifi->state) {
        case WIFI_STATE_CONNECTING:
            if (link == CYW43_LINK_UP) {
                capture_address(wifi);
                wifi_retry_reset(&wifi->retry);
                wifi->state = WIFI_STATE_CONNECTED;
                break;
            }
            /* Negative statuses are the radio reporting a definite failure —
               wrong passphrase, no such network — so there is no point waiting
               out the timeout. */
            if (link < 0) {
                handle_failure(wifi);
                break;
            }
            if ((uint32_t)(now_ms() - wifi->attempt_started_ms) >=
                wifi->config.attempt_timeout_ms) {
                handle_failure(wifi);
            }
            break;

        case WIFI_STATE_CONNECTED:
            /* A link that drops is noticed here and re-established without the
               caller doing anything, which is the whole point of polling. */
            if (link != CYW43_LINK_UP) {
                handle_failure(wifi);
            } else {
                capture_address(wifi);
            }
            break;

        case WIFI_STATE_WAITING:
            if (wifi_retry_due(&wifi->retry, now_ms())) {
                if (begin_attempt(wifi) != WIFI_OK) {
                    handle_failure(wifi);
                }
            }
            break;

        case WIFI_STATE_IDLE:
        case WIFI_STATE_GAVE_UP:
        default:
            break;
    }
}

int32_t wifi_rssi(wifi_t *wifi)
{
    if (wifi == NULL || !wifi_is_connected(wifi)) {
        return 0;
    }

    int32_t rssi = 0;
    if (cyw43_wifi_get_rssi(&cyw43_state, &rssi) != 0) {
        return 0;
    }
    return rssi;
}

#endif /* WIFI_SUPPORTED */
