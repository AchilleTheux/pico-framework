/*
 * mqtt - a thin, poll-driven wrapper around lwIP's native MQTT client.
 *
 * What DESIGN_DOC.md section 18 lists as a future component, sitting where
 * section 17's WiFi note says higher-level networking belongs: separate from
 * connection management. This component gets a broker session; wifi.c's job
 * ends at "there is a working link and here is its address", and it stays
 * that way here too -- mqtt_poll() never touches the radio or the netif, only
 * the MQTT session sitting on top of whatever link is already up.
 *
 * Non-blocking, like every other component here. mqtt_connect() starts a DNS
 * lookup and a broker connection and returns at once; mqtt_poll() drives the
 * reconnect state machine and must be called regularly, the same contract as
 * wifi_poll(). It does not call wifi_poll() or cyw43_arch_poll() itself --
 * lwIP's core processing, which is what actually resolves the DNS lookup and
 * moves the connection along, happens as a side effect of whatever already
 * polls the link. A caller with no working link yet is not a caller with
 * nothing to wait for: mqtt_connect() can be called before wifi_is_connected()
 * is true, and the connection attempt will simply keep failing and retrying
 * -- via this component's own backoff -- until the link comes up.
 *
 * BOARDS WITHOUT A RADIO
 *
 * This component depends on wifi for the only network stack the framework
 * currently has (lwIP over the CYW43), the same way ax12 depends on
 * half_duplex_uart for a transport it does not implement itself. It compiles
 * for every board; MQTT_SUPPORTED tracks WIFI_SUPPORTED, and without a radio
 * every call returns MQTT_ERR_UNSUPPORTED. Check the macro rather than
 * discovering it at runtime.
 *
 * RECONNECTION
 *
 * A dropped broker session is retried on the exact backoff wifi.c already
 * implements and tests -- wifi_retry_t, reused rather than reimplemented,
 * since "wait longer after each failure, cap it, survive the millisecond
 * counter's wrap" is exactly the same decision for a broker as for an access
 * point. See components/wifi/wifi_policy.h.
 *
 * NAMING
 *
 * mqtt.c includes lwIP's own lwip/apps/mqtt.h, which already defines
 * mqtt_publish(), mqtt_subscribe(), mqtt_unsubscribe() and mqtt_disconnect()
 * as real functions or macros. This header's equivalents are named
 * mqtt_publish_message(), mqtt_subscribe_topic(), mqtt_unsubscribe_topic()
 * and mqtt_close() instead of colliding with them -- mqtt_subscribe and
 * mqtt_unsubscribe in particular are macros in lwIP's header, so reusing
 * those names here would not just shadow a symbol, it would mangle this
 * header's own declarations wherever both are visible.
 */

#ifndef PICO_FRAMEWORK_MQTT_H
#define PICO_FRAMEWORK_MQTT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "mqtt_policy.h"
#include "wifi.h"
#include "wifi_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this build has a network stack to talk to. */
#define MQTT_SUPPORTED WIFI_SUPPORTED

#define MQTT_DEFAULT_PORT 1883u
#define MQTT_DEFAULT_KEEP_ALIVE_S 60u

/* Longest incoming publish this component will reassemble in one piece.
   Longer messages are still reported -- truncated -- through
   mqtt_messages_dropped() rather than delivered partially and silently. */
#ifndef MQTT_MAX_MESSAGE_LENGTH
#define MQTT_MAX_MESSAGE_LENGTH 256u
#endif

typedef enum {
    MQTT_OK = 0,
    MQTT_ERR_INVALID_ARG,
    MQTT_ERR_UNSUPPORTED,    /* no network stack on this board */
    MQTT_ERR_NOT_CONNECTED,
    MQTT_ERR_NO_MEMORY,      /* lwIP had no mqtt_client_t left to give */
    MQTT_ERR_FAILED,         /* lwIP or the broker refused the request */
} mqtt_result_t;

typedef enum {
    MQTT_STATE_IDLE = 0,      /* mqtt_connect() not called, or mqtt_close()d */
    MQTT_STATE_RESOLVING,     /* DNS lookup in flight */
    MQTT_STATE_CONNECTING,    /* TCP + MQTT CONNECT in flight */
    MQTT_STATE_CONNECTED,
    MQTT_STATE_WAITING,       /* backing off before the next attempt */
    MQTT_STATE_GAVE_UP,
} mqtt_state_t;

/*
 * Called with a complete incoming publish: `topic` is a zero-terminated
 * string, `payload` points at `length` bytes valid only for the duration of
 * the call. Never called for a message longer than MQTT_MAX_MESSAGE_LENGTH --
 * that is counted in mqtt_messages_dropped() instead, on the view that a
 * truncated payload handed to a caller expecting the whole thing is worse
 * than no callback at all.
 */
typedef void (*mqtt_message_cb_t)(void *arg, const char *topic,
                                  const uint8_t *payload, size_t length);

typedef struct {
    /*
     * Borrowed, not copied, so all of these must outlive the connection --
     * the same trade wifi_config_t makes, for the same reason: copying a
     * password means a second place for it to sit in memory.
     */
    const char *broker_host;   /* hostname or dotted-quad */
    uint16_t broker_port;      /* 0 => MQTT_DEFAULT_PORT */

    const char *client_id;     /* must be non-empty; see mqtt_check_client_id() */
    const char *username;      /* NULL if not used */
    const char *password;      /* NULL if not used */
    uint16_t keep_alive_s;     /* 0 => MQTT_DEFAULT_KEEP_ALIVE_S */

    /* Published by the broker if this client disappears without a clean
       mqtt_close(). NULL or empty disables it. */
    const char *will_topic;
    const char *will_message;
    uint8_t will_qos;
    bool will_retain;

    /* Optional; NULL means incoming publishes are simply not delivered
       anywhere, which is a legal way to run a publish-only client. */
    mqtt_message_cb_t on_message;
    void *on_message_arg;

    wifi_retry_config_t retry;
} mqtt_config_t;

typedef struct {
    mqtt_config_t config;
    mqtt_state_t state;
    wifi_retry_t retry;

    /*
     * lwIP's mqtt_client_t*, kept as void* so this header needs no lwIP
     * include and so compiles unchanged on a board with no network stack at
     * all. mqtt.c is the only file that casts it back.
     */
    void *client;

    /* Resolved broker address, host byte order irrelevant here since it is
       only ever round-tripped through lwIP's own u32 accessors. 0 before the
       first successful lookup. */
    uint32_t broker_ipv4;

    char message_topic[MQTT_TOPIC_MAX_LENGTH + 1];
    uint8_t message_buffer[MQTT_MAX_MESSAGE_LENGTH];
    size_t message_length;
    bool message_overflowed;
    uint32_t messages_dropped;

    bool initialised;
} mqtt_t;

/* Zero the instance. Allocates nothing yet -- lwIP's mqtt_client_t is created
   lazily by the first mqtt_connect(). */
mqtt_result_t mqtt_init(mqtt_t *mqtt);

void mqtt_deinit(mqtt_t *mqtt);

/*
 * Begin connecting: resolve `config->broker_host` and open a session.
 * Returns as soon as the attempt has started; watch mqtt_state() or
 * mqtt_is_connected() for the outcome, the same contract as wifi_connect().
 *
 * `client_id`, and `will_topic` if set, are checked against what the MQTT
 * spec allows before anything is sent, so a mistake is reported as such
 * rather than as a broker that mysteriously refuses this device.
 *
 * A DNS or connect failure does not fail this call -- it starts the retry
 * loop instead, the same way a lost connection later does. There is nothing
 * unusual about the network not being ready yet when this is called; that is
 * exactly the case mqtt_poll()'s backoff exists for.
 */
mqtt_result_t mqtt_connect(mqtt_t *mqtt, const mqtt_config_t *config);

/* Stop trying, and close the session if one is open. */
mqtt_result_t mqtt_close(mqtt_t *mqtt);

/*
 * Drive the reconnect state machine. Call it every time round the main loop,
 * alongside wifi_poll() -- this does not poll the radio or lwIP itself, only
 * this component's own retry timer, so it is not a substitute for wifi_poll()
 * and has nothing to do without something else already polling the link.
 */
void mqtt_poll(mqtt_t *mqtt);

/* ---------------------------------------------------------------------------
 * Publishing and subscribing
 * -------------------------------------------------------------------------*/

/* MQTT_ERR_NOT_CONNECTED rather than queuing -- a caller that wants store-
   and-forward across an outage owns that buffering, since only it knows how
   much is worth keeping. */
mqtt_result_t mqtt_publish_message(mqtt_t *mqtt, const char *topic,
                                   const void *payload, uint16_t length,
                                   uint8_t qos, bool retain);

mqtt_result_t mqtt_subscribe_topic(mqtt_t *mqtt, const char *topic, uint8_t qos);
mqtt_result_t mqtt_unsubscribe_topic(mqtt_t *mqtt, const char *topic);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

static inline mqtt_state_t mqtt_state(const mqtt_t *mqtt)
{
    return mqtt->state;
}

static inline bool mqtt_is_connected(const mqtt_t *mqtt)
{
    return mqtt->state == MQTT_STATE_CONNECTED;
}

/* How many attempts the current outage has taken. Zero when connected. */
static inline uint32_t mqtt_attempts(const mqtt_t *mqtt)
{
    return wifi_retry_attempts(&mqtt->retry);
}

/* Complete incoming messages dropped for arriving longer than
   MQTT_MAX_MESSAGE_LENGTH. Counted rather than silently truncated. */
static inline uint32_t mqtt_messages_dropped(const mqtt_t *mqtt)
{
    return mqtt->messages_dropped;
}

const char *mqtt_state_name(mqtt_state_t state);
const char *mqtt_result_name(mqtt_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MQTT_H */
