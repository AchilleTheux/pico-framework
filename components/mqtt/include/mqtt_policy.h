/*
 * mqtt_policy - whether a topic, filter, or client id is one the MQTT spec
 * actually allows.
 *
 * lwIP's own mqtt_publish()/mqtt_sub_unsub() take whatever string they are
 * given; a stray '#' in a publish topic, or a '+' sharing a level with other
 * text in a subscribe filter, is not rejected there. It either does nothing
 * useful at the broker or silently subscribes to more (or less) than intended
 * -- exactly the kind of mistake worth catching before it reaches the wire.
 *
 * No Pico SDK or lwIP dependency, so this is unit-tested on the host.
 */

#ifndef PICO_FRAMEWORK_MQTT_POLICY_H
#define PICO_FRAMEWORK_MQTT_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* MQTT topics are length-prefixed with 16 bits on the wire, so 65535 bytes is
   the protocol ceiling. This is a much smaller practical limit, chosen to fit
   comfortably in a stack buffer and in mqtt_t's fixed-size storage. */
#ifndef MQTT_TOPIC_MAX_LENGTH
#define MQTT_TOPIC_MAX_LENGTH 128u
#endif

/* MQTT 3.1 limited client ids to 23 characters; 3.1.1 lifted that and most
   brokers (Mosquitto included) accept far more. This is a practical ceiling,
   not a protocol one. */
#ifndef MQTT_CLIENT_ID_MAX_LENGTH
#define MQTT_CLIENT_ID_MAX_LENGTH 64u
#endif

typedef enum {
    MQTT_TOPIC_OK = 0,
    MQTT_TOPIC_EMPTY,
    MQTT_TOPIC_TOO_LONG,
    MQTT_TOPIC_WILDCARD_NOT_ALLOWED,     /* '+' or '#' in a publish topic */
    MQTT_TOPIC_BAD_WILDCARD_PLACEMENT,   /* '+' not alone in its level, or
                                             '#' not alone and last */
} mqtt_topic_result_t;

const char *mqtt_topic_result_name(mqtt_topic_result_t result);

/*
 * A topic a message may be published to. Wildcards mean nothing to a
 * publish -- they only work as-typed to a broker that does no filter parsing
 * of its own -- so '+' and '#' are rejected here rather than sent.
 */
mqtt_topic_result_t mqtt_check_publish_topic(const char *topic);

/*
 * A filter a client may subscribe to. '+' matches exactly one level and must
 * occupy it alone; '#' matches every remaining level and must be both alone
 * in its level and the last one. Anything else is a filter that looks like it
 * does one thing and does another.
 */
mqtt_topic_result_t mqtt_check_subscribe_filter(const char *topic);

typedef enum {
    MQTT_CLIENT_ID_OK = 0,
    MQTT_CLIENT_ID_EMPTY,
    MQTT_CLIENT_ID_TOO_LONG,
} mqtt_client_id_result_t;

const char *mqtt_client_id_result_name(mqtt_client_id_result_t result);

/* Every broker on the wire needs a client id; an empty one is a connection
   that will not be accepted, reported here rather than left to look like a
   broker refusing this device for no reason. */
mqtt_client_id_result_t mqtt_check_client_id(const char *client_id);

/* MQTT defines exactly three delivery levels. */
bool mqtt_qos_is_valid(uint8_t qos);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_MQTT_POLICY_H */
