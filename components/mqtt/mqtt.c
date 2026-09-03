#include <string.h>

#include "pico/stdlib.h"

#include "mqtt.h"

#if MQTT_SUPPORTED
#include "lwip/apps/mqtt.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#endif

const char *mqtt_state_name(mqtt_state_t state)
{
    switch (state) {
        case MQTT_STATE_IDLE:       return "idle";
        case MQTT_STATE_RESOLVING:  return "resolving";
        case MQTT_STATE_CONNECTING: return "connecting";
        case MQTT_STATE_CONNECTED:  return "connected";
        case MQTT_STATE_WAITING:    return "waiting to retry";
        case MQTT_STATE_GAVE_UP:    return "gave up";
        default:                    return "unknown";
    }
}

const char *mqtt_result_name(mqtt_result_t result)
{
    switch (result) {
        case MQTT_OK:                 return "ok";
        case MQTT_ERR_INVALID_ARG:    return "invalid argument";
        case MQTT_ERR_UNSUPPORTED:    return "no network stack on this board";
        case MQTT_ERR_NOT_CONNECTED:  return "not connected";
        case MQTT_ERR_NO_MEMORY:      return "no mqtt client left to give";
        case MQTT_ERR_FAILED:         return "refused";
        default:                      return "unknown";
    }
}

#if !MQTT_SUPPORTED

/*
 * No network stack on this board -- see mqtt.h. Every call reports
 * MQTT_ERR_UNSUPPORTED rather than failing to link.
 */

mqtt_result_t mqtt_init(mqtt_t *mqtt)
{
    if (mqtt != NULL) {
        memset(mqtt, 0, sizeof(*mqtt));
    }
    return MQTT_ERR_UNSUPPORTED;
}

void mqtt_deinit(mqtt_t *mqtt) { (void)mqtt; }

mqtt_result_t mqtt_connect(mqtt_t *mqtt, const mqtt_config_t *config)
{
    (void)mqtt;
    (void)config;
    return MQTT_ERR_UNSUPPORTED;
}

mqtt_result_t mqtt_close(mqtt_t *mqtt)
{
    (void)mqtt;
    return MQTT_ERR_UNSUPPORTED;
}

void mqtt_poll(mqtt_t *mqtt) { (void)mqtt; }

mqtt_result_t mqtt_publish_message(mqtt_t *mqtt, const char *topic,
                                   const void *payload, uint16_t length,
                                   uint8_t qos, bool retain)
{
    (void)mqtt; (void)topic; (void)payload; (void)length; (void)qos; (void)retain;
    return MQTT_ERR_UNSUPPORTED;
}

mqtt_result_t mqtt_subscribe_topic(mqtt_t *mqtt, const char *topic, uint8_t qos)
{
    (void)mqtt; (void)topic; (void)qos;
    return MQTT_ERR_UNSUPPORTED;
}

mqtt_result_t mqtt_unsubscribe_topic(mqtt_t *mqtt, const char *topic)
{
    (void)mqtt; (void)topic;
    return MQTT_ERR_UNSUPPORTED;
}

#else /* MQTT_SUPPORTED */

static uint32_t now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

static inline mqtt_client_t *client_of(mqtt_t *mqtt)
{
    return (mqtt_client_t *)mqtt->client;
}

static void handle_failure(mqtt_t *mqtt)
{
    if (wifi_retry_exhausted(&mqtt->retry)) {
        mqtt->state = MQTT_STATE_GAVE_UP;
        return;
    }
    wifi_retry_fail(&mqtt->retry, now_ms());
    mqtt->state = MQTT_STATE_WAITING;
}

static void mqtt_connection_status_handler(mqtt_client_t *client, void *arg,
                                           mqtt_connection_status_t status);
static void on_incoming_publish(void *arg, const char *topic, u32_t tot_len);
static void on_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags);

/* Open the TCP connection and send MQTT CONNECT to whatever mqtt->broker_ipv4
   currently holds. Creates the lwIP client on first use; the same client is
   reused across reconnects, since lwIP has only a handful to give out. */
static mqtt_result_t start_connect(mqtt_t *mqtt)
{
    if (mqtt->client == NULL) {
        mqtt_client_t *client = mqtt_client_new();
        if (client == NULL) {
            return MQTT_ERR_NO_MEMORY;
        }
        mqtt->client = client;

        /* Must be set on this exact client before anything can arrive --
           there is no default, and lwIP silently discards an incoming
           publish with no callback registered to receive it. */
        mqtt_set_inpub_callback(client, on_incoming_publish, on_incoming_data, mqtt);
    }

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, mqtt->broker_ipv4);

    struct mqtt_connect_client_info_t info;
    memset(&info, 0, sizeof(info));
    info.client_id = mqtt->config.client_id;
    info.client_user = (mqtt->config.username != NULL && mqtt->config.username[0] != '\0')
                            ? mqtt->config.username : NULL;
    info.client_pass = (mqtt->config.password != NULL && mqtt->config.password[0] != '\0')
                            ? mqtt->config.password : NULL;
    info.keep_alive = mqtt->config.keep_alive_s;
    if (mqtt->config.will_topic != NULL && mqtt->config.will_topic[0] != '\0') {
        info.will_topic = mqtt->config.will_topic;
        info.will_msg = mqtt->config.will_message;
        info.will_qos = mqtt->config.will_qos;
        info.will_retain = mqtt->config.will_retain ? 1u : 0u;
    }

    const err_t err = mqtt_client_connect(client_of(mqtt), &addr, mqtt->config.broker_port,
                                          mqtt_connection_status_handler, mqtt, &info);
    if (err != ERR_OK) {
        return MQTT_ERR_FAILED;
    }
    mqtt->state = MQTT_STATE_CONNECTING;
    return MQTT_OK;
}

static void on_dns(const char *name, const ip_addr_t *ipaddr, void *arg);

/* Resolve the broker's address, then hand off to start_connect() -- either
   at once, if the name was already cached, or from on_dns() once lwIP's
   lookup completes. */
static void begin_attempt(mqtt_t *mqtt)
{
    ip_addr_t resolved;
    const err_t err = dns_gethostbyname(mqtt->config.broker_host, &resolved, on_dns, mqtt);

    if (err == ERR_OK) {
        mqtt->broker_ipv4 = ip4_addr_get_u32(ip_2_ip4(&resolved));
        if (start_connect(mqtt) != MQTT_OK) {
            handle_failure(mqtt);
        }
        return;
    }
    if (err == ERR_INPROGRESS) {
        mqtt->state = MQTT_STATE_RESOLVING;
        return;
    }
    handle_failure(mqtt);
}

static void on_dns(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    mqtt_t *mqtt = (mqtt_t *)arg;

    if (ipaddr == NULL) {
        handle_failure(mqtt);
        return;
    }
    mqtt->broker_ipv4 = ip4_addr_get_u32(ip_2_ip4(ipaddr));
    if (start_connect(mqtt) != MQTT_OK) {
        handle_failure(mqtt);
    }
}

static void mqtt_connection_status_handler(mqtt_client_t *client, void *arg,
                                           mqtt_connection_status_t status)
{
    (void)client;
    mqtt_t *mqtt = (mqtt_t *)arg;

    if (status == MQTT_CONNECT_ACCEPTED) {
        wifi_retry_reset(&mqtt->retry);
        mqtt->state = MQTT_STATE_CONNECTED;
        mqtt->sessions++;

        /*
         * State first, callback second: on_connect exists to subscribe and to
         * re-announce, and both of those go through calls that refuse unless
         * this instance already believes it is connected.
         */
        if (mqtt->config.on_connect != NULL) {
            mqtt->config.on_connect(mqtt->config.on_connect_arg);
        }
        return;
    }
    handle_failure(mqtt);
}

/* Reassemble a possibly-fragmented incoming publish into one callback. lwIP
   splits topic-arrival from data-arrival because a publish can be larger than
   it wants to buffer; this component hides that from callers, at the cost of
   needing a bound on how much of one message it will hold at once. */
static void on_incoming_publish(void *arg, const char *topic, u32_t tot_len)
{
    mqtt_t *mqtt = (mqtt_t *)arg;
    (void)tot_len;

    mqtt->message_length = 0;
    mqtt->message_overflowed = false;
    snprintf(mqtt->message_topic, sizeof(mqtt->message_topic), "%s", topic);
}

static void on_incoming_data(void *arg, const u8_t *data, u16_t len, u8_t flags)
{
    mqtt_t *mqtt = (mqtt_t *)arg;

    const size_t space = sizeof(mqtt->message_buffer) - mqtt->message_length;
    const size_t copy = (len < space) ? (size_t)len : space;
    if (copy > 0) {
        memcpy(mqtt->message_buffer + mqtt->message_length, data, copy);
        mqtt->message_length += copy;
    }
    if ((size_t)len > copy) {
        mqtt->message_overflowed = true;
    }

    if ((flags & MQTT_DATA_FLAG_LAST) == 0) {
        return;
    }

    if (mqtt->message_overflowed) {
        mqtt->messages_dropped++;
    } else if (mqtt->config.on_message != NULL) {
        mqtt->config.on_message(mqtt->config.on_message_arg, mqtt->message_topic,
                                mqtt->message_buffer, mqtt->message_length);
    }
    mqtt->message_length = 0;
    mqtt->message_overflowed = false;
}

mqtt_result_t mqtt_init(mqtt_t *mqtt)
{
    if (mqtt == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }
    memset(mqtt, 0, sizeof(*mqtt));
    mqtt->state = MQTT_STATE_IDLE;
    mqtt->initialised = true;
    return MQTT_OK;
}

void mqtt_deinit(mqtt_t *mqtt)
{
    if (mqtt == NULL || !mqtt->initialised) {
        return;
    }
    if (mqtt->client != NULL) {
        mqtt_client_free(client_of(mqtt));
        mqtt->client = NULL;
    }
    mqtt->initialised = false;
    mqtt->state = MQTT_STATE_IDLE;
}

mqtt_result_t mqtt_connect(mqtt_t *mqtt, const mqtt_config_t *config)
{
    if (mqtt == NULL || !mqtt->initialised || config == NULL) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (config->broker_host == NULL || config->broker_host[0] == '\0') {
        return MQTT_ERR_INVALID_ARG;
    }
    if (mqtt_check_client_id(config->client_id) != MQTT_CLIENT_ID_OK) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (config->will_topic != NULL && config->will_topic[0] != '\0') {
        if (mqtt_check_publish_topic(config->will_topic) != MQTT_TOPIC_OK) {
            return MQTT_ERR_INVALID_ARG;
        }
        if (!mqtt_qos_is_valid(config->will_qos)) {
            return MQTT_ERR_INVALID_ARG;
        }
    }

    mqtt->config = *config;
    if (mqtt->config.broker_port == 0) {
        mqtt->config.broker_port = MQTT_DEFAULT_PORT;
    }
    if (mqtt->config.keep_alive_s == 0) {
        mqtt->config.keep_alive_s = MQTT_DEFAULT_KEEP_ALIVE_S;
    }

    wifi_retry_init(&mqtt->retry, &mqtt->config.retry);
    mqtt->message_length = 0;
    mqtt->message_overflowed = false;

    if (mqtt->client != NULL) {
        mqtt_set_inpub_callback(client_of(mqtt), on_incoming_publish, on_incoming_data, mqtt);
    }

    begin_attempt(mqtt);
    return MQTT_OK;
}

mqtt_result_t mqtt_close(mqtt_t *mqtt)
{
    if (mqtt == NULL || !mqtt->initialised) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (mqtt->client != NULL) {
        mqtt_disconnect(client_of(mqtt));
    }
    wifi_retry_reset(&mqtt->retry);
    mqtt->state = MQTT_STATE_IDLE;
    return MQTT_OK;
}

void mqtt_poll(mqtt_t *mqtt)
{
    if (mqtt == NULL || !mqtt->initialised) {
        return;
    }
    if (mqtt->state == MQTT_STATE_WAITING && wifi_retry_due(&mqtt->retry, now_ms())) {
        begin_attempt(mqtt);
    }
}

mqtt_result_t mqtt_publish_message(mqtt_t *mqtt, const char *topic,
                                   const void *payload, uint16_t length,
                                   uint8_t qos, bool retain)
{
    if (mqtt == NULL || !mqtt->initialised || (payload == NULL && length != 0)) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (mqtt_check_publish_topic(topic) != MQTT_TOPIC_OK) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (!mqtt_qos_is_valid(qos)) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (!mqtt_is_connected(mqtt)) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    const err_t err = mqtt_publish(client_of(mqtt), topic, payload, length, qos,
                                   retain ? 1u : 0u, NULL, NULL);
    return (err == ERR_OK) ? MQTT_OK : MQTT_ERR_FAILED;
}

mqtt_result_t mqtt_subscribe_topic(mqtt_t *mqtt, const char *topic, uint8_t qos)
{
    if (mqtt == NULL || !mqtt->initialised) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (mqtt_check_subscribe_filter(topic) != MQTT_TOPIC_OK) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (!mqtt_qos_is_valid(qos)) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (!mqtt_is_connected(mqtt)) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    const err_t err = mqtt_sub_unsub(client_of(mqtt), topic, qos, NULL, NULL, 1);
    return (err == ERR_OK) ? MQTT_OK : MQTT_ERR_FAILED;
}

mqtt_result_t mqtt_unsubscribe_topic(mqtt_t *mqtt, const char *topic)
{
    if (mqtt == NULL || !mqtt->initialised) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (mqtt_check_subscribe_filter(topic) != MQTT_TOPIC_OK) {
        return MQTT_ERR_INVALID_ARG;
    }
    if (!mqtt_is_connected(mqtt)) {
        return MQTT_ERR_NOT_CONNECTED;
    }

    const err_t err = mqtt_sub_unsub(client_of(mqtt), topic, 0, NULL, NULL, 0);
    return (err == ERR_OK) ? MQTT_OK : MQTT_ERR_FAILED;
}

#endif /* MQTT_SUPPORTED */
