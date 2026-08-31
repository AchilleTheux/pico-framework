/*
 * wifi - station-mode connection management for the Pico W's CYW43 radio.
 *
 * What DESIGN_DOC.md section 17 asks for: initialisation, connection
 * management, credentials, reconnect behaviour and network status — with
 * higher-level TCP and UDP kept separate, so this component's job ends at
 * "there is a working link and here is its address".
 *
 * Non-blocking. wifi_poll() drives both lwIP and the connection state machine
 * and returns at once, so a control loop keeps running while the radio
 * associates — which takes seconds, and on a robot is seconds nothing else can
 * afford to stop for. It matches the rest of the framework: cli_poll(),
 * vl53l0x_data_ready(), servo transactions.
 *
 * Credentials are passed in, never compiled in. The framework's answer for
 * where they come from is persistent_config, set once over the console; that is
 * deliberately not a dependency of this component, so a caller can source them
 * however it likes.
 *
 * BOARDS WITHOUT A RADIO
 *
 * The component compiles for every board so that it need not be conditionally
 * registered, but on a board with no CYW43 every call returns
 * WIFI_ERR_UNSUPPORTED and WIFI_SUPPORTED is 0. Check that rather than
 * discovering it at runtime.
 */

#ifndef PICO_FRAMEWORK_WIFI_H
#define PICO_FRAMEWORK_WIFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "wifi_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this build has a CYW43 radio to talk to. */
#if defined(CYW43_WL_GPIO_LED_PIN) || defined(PICO_CYW43_SUPPORTED)
#define WIFI_SUPPORTED 1
#else
#define WIFI_SUPPORTED 0
#endif

/* Longest a dotted-quad address needs, terminator included. */
#define WIFI_ADDRESS_LENGTH 16u

typedef enum {
    WIFI_OK = 0,
    WIFI_ERR_INVALID_ARG,
    WIFI_ERR_UNSUPPORTED,     /* no radio on this board */
    WIFI_ERR_RADIO,           /* the CYW43 refused to start */
    WIFI_ERR_CREDENTIALS,     /* see wifi_check_credentials() */
    WIFI_ERR_NOT_CONNECTED,
    WIFI_ERR_GAVE_UP,         /* the retry budget ran out */
} wifi_result_t;

typedef enum {
    WIFI_STATE_IDLE = 0,      /* radio up, not trying to associate */
    WIFI_STATE_CONNECTING,
    WIFI_STATE_CONNECTED,
    WIFI_STATE_WAITING,       /* backing off before the next attempt */
    WIFI_STATE_GAVE_UP,
} wifi_state_t;

typedef struct {
    /*
     * Borrowed, not copied, so both must outlive the connection. Copying them
     * would mean a fixed maximum length here and a second place for a
     * passphrase to sit in memory.
     */
    const char *ssid;
    const char *password;     /* NULL or empty for an open network */

    /* Reported to the network, so a robot is findable by name rather than by
       hunting for its address. NULL leaves the SDK's default. */
    const char *hostname;

    /* How long one association attempt may take before it counts as failed. */
    uint32_t attempt_timeout_ms;

    wifi_retry_config_t retry;
} wifi_config_t;

#define WIFI_DEFAULT_ATTEMPT_TIMEOUT_MS 15000u

typedef struct {
    wifi_config_t config;
    wifi_retry_t retry;
    wifi_state_t state;

    uint32_t attempt_started_ms;
    char address[WIFI_ADDRESS_LENGTH];

    bool radio_started;
    bool initialised;
} wifi_t;

/*
 * Start the radio. Does not connect; call wifi_connect() for that.
 *
 * Takes a moment — the CYW43's firmware is uploaded from flash — and is the
 * only blocking call here.
 */
wifi_result_t wifi_init(wifi_t *wifi);

/* Stop the radio. */
void wifi_deinit(wifi_t *wifi);

/*
 * Begin associating. Returns as soon as the attempt has started; watch
 * wifi_state() or wifi_is_connected() for the outcome.
 *
 * The credentials are checked first, so a passphrase that WPA2 could never
 * accept is reported as WIFI_ERR_CREDENTIALS rather than as a network that
 * will not associate.
 */
wifi_result_t wifi_connect(wifi_t *wifi, const wifi_config_t *config);

/* Stop trying, and drop the link if there is one. */
wifi_result_t wifi_disconnect(wifi_t *wifi);

/*
 * Drive lwIP and the connection state machine. Call it every time round the
 * main loop; it never blocks.
 *
 * This is also what retries: a link that drops is noticed here and
 * re-established according to the retry policy, without the caller doing
 * anything.
 */
void wifi_poll(wifi_t *wifi);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

static inline wifi_state_t wifi_state(const wifi_t *wifi)
{
    return wifi->state;
}

static inline bool wifi_is_connected(const wifi_t *wifi)
{
    return wifi->state == WIFI_STATE_CONNECTED;
}

/*
 * The dotted-quad address, or "0.0.0.0" when there is no link. Always a valid
 * string, so it can go straight into a status line.
 */
const char *wifi_address(const wifi_t *wifi);

/* Signal strength in dBm, or 0 when not connected. Typically -30 close to an
   access point and -80 at the edge of usable. */
int32_t wifi_rssi(wifi_t *wifi);

/* How many attempts the current outage has taken. Zero when connected. */
static inline uint32_t wifi_attempts(const wifi_t *wifi)
{
    return wifi_retry_attempts(&wifi->retry);
}

const char *wifi_state_name(wifi_state_t state);
const char *wifi_result_name(wifi_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_WIFI_H */
