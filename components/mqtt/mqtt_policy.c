#include <string.h>

#include "mqtt_policy.h"

const char *mqtt_topic_result_name(mqtt_topic_result_t result)
{
    switch (result) {
        case MQTT_TOPIC_OK:                        return "ok";
        case MQTT_TOPIC_EMPTY:                     return "empty";
        case MQTT_TOPIC_TOO_LONG:                  return "longer than the limit";
        case MQTT_TOPIC_WILDCARD_NOT_ALLOWED:       return "wildcard in a publish topic";
        case MQTT_TOPIC_BAD_WILDCARD_PLACEMENT:     return "wildcard not alone in its level";
        default:                                   return "unknown";
    }
}

mqtt_topic_result_t mqtt_check_publish_topic(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') {
        return MQTT_TOPIC_EMPTY;
    }

    const size_t length = strlen(topic);
    if (length > MQTT_TOPIC_MAX_LENGTH) {
        return MQTT_TOPIC_TOO_LONG;
    }

    for (size_t i = 0; i < length; i++) {
        if (topic[i] == '+' || topic[i] == '#') {
            return MQTT_TOPIC_WILDCARD_NOT_ALLOWED;
        }
    }
    return MQTT_TOPIC_OK;
}

mqtt_topic_result_t mqtt_check_subscribe_filter(const char *topic)
{
    if (topic == NULL || topic[0] == '\0') {
        return MQTT_TOPIC_EMPTY;
    }

    const size_t length = strlen(topic);
    if (length > MQTT_TOPIC_MAX_LENGTH) {
        return MQTT_TOPIC_TOO_LONG;
    }

    /*
     * Walk one level at a time, checking each against the two wildcard rules
     * rather than just scanning for the characters -- "sport/tennis+" is not
     * legal even though "sport/+/results" is, and the difference is entirely
     * about what else shares the level.
     */
    const char *level_start = topic;
    for (const char *p = topic; ; p++) {
        if (*p == '/' || *p == '\0') {
            const size_t level_length = (size_t)(p - level_start);
            bool has_plus = false;
            bool has_hash = false;
            for (size_t i = 0; i < level_length; i++) {
                if (level_start[i] == '+') has_plus = true;
                if (level_start[i] == '#') has_hash = true;
            }

            if (has_plus && level_length != 1) {
                return MQTT_TOPIC_BAD_WILDCARD_PLACEMENT;
            }
            if (has_hash) {
                /* Must be alone in its level, and that level must be the
                   last one -- '#' matches everything below it, so anything
                   typed after it could never be reached. */
                if (level_length != 1 || *p != '\0') {
                    return MQTT_TOPIC_BAD_WILDCARD_PLACEMENT;
                }
            }

            if (*p == '\0') {
                break;
            }
            level_start = p + 1;
        }
    }
    return MQTT_TOPIC_OK;
}

const char *mqtt_client_id_result_name(mqtt_client_id_result_t result)
{
    switch (result) {
        case MQTT_CLIENT_ID_OK:         return "ok";
        case MQTT_CLIENT_ID_EMPTY:      return "empty";
        case MQTT_CLIENT_ID_TOO_LONG:   return "longer than the limit";
        default:                        return "unknown";
    }
}

mqtt_client_id_result_t mqtt_check_client_id(const char *client_id)
{
    if (client_id == NULL || client_id[0] == '\0') {
        return MQTT_CLIENT_ID_EMPTY;
    }
    if (strlen(client_id) > MQTT_CLIENT_ID_MAX_LENGTH) {
        return MQTT_CLIENT_ID_TOO_LONG;
    }
    return MQTT_CLIENT_ID_OK;
}

bool mqtt_qos_is_valid(uint8_t qos)
{
    return qos <= 2;
}
