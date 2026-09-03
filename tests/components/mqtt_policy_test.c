/*
 * Host-side tests for MQTT topic, client-id and QoS validation.
 *
 * These are the parts of MQTT that are decisions rather than wire protocol --
 * lwIP's own mqtt_publish()/mqtt_sub_unsub() send whatever string they are
 * given, so a topic that looks legal but does the wrong thing (a wildcard in
 * a publish, a '+' sharing a level with other text) would otherwise only show
 * up as a broker doing something unexpected.
 */

#include <string.h>

#include "test.h"

#include "mqtt_policy.h"

/* ---------------------------------------------------------------------------
 * Publish topics
 * -------------------------------------------------------------------------*/

TEST(a_reasonable_publish_topic_is_accepted)
{
    CHECK_EQ_INT(mqtt_check_publish_topic("robot/status"), MQTT_TOPIC_OK);
}

TEST(an_empty_publish_topic_is_rejected)
{
    CHECK_EQ_INT(mqtt_check_publish_topic(NULL), MQTT_TOPIC_EMPTY);
    CHECK_EQ_INT(mqtt_check_publish_topic(""), MQTT_TOPIC_EMPTY);
}

TEST(a_publish_topic_cannot_carry_a_wildcard)
{
    /*
     * These would not be rejected by lwIP itself -- it just sends the bytes.
     * A broker that received "robot/+/status" as a literal publish topic
     * would not tell anyone this was probably meant as a subscribe filter.
     */
    CHECK_EQ_INT(mqtt_check_publish_topic("robot/+/status"), MQTT_TOPIC_WILDCARD_NOT_ALLOWED);
    CHECK_EQ_INT(mqtt_check_publish_topic("robot/#"), MQTT_TOPIC_WILDCARD_NOT_ALLOWED);
}

TEST(a_publish_topic_over_the_limit_is_rejected)
{
    char topic[MQTT_TOPIC_MAX_LENGTH + 2];
    memset(topic, 'a', sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';
    CHECK_EQ_INT(mqtt_check_publish_topic(topic), MQTT_TOPIC_TOO_LONG);

    topic[MQTT_TOPIC_MAX_LENGTH] = '\0';
    CHECK_EQ_INT(mqtt_check_publish_topic(topic), MQTT_TOPIC_OK);
}

/* ---------------------------------------------------------------------------
 * Subscribe filters
 * -------------------------------------------------------------------------*/

TEST(plain_and_wildcard_filters_that_are_legal_are_accepted)
{
    CHECK_EQ_INT(mqtt_check_subscribe_filter("robot/status"), MQTT_TOPIC_OK);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("robot/+/status"), MQTT_TOPIC_OK);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("robot/#"), MQTT_TOPIC_OK);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("#"), MQTT_TOPIC_OK);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("+"), MQTT_TOPIC_OK);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("+/+/+"), MQTT_TOPIC_OK);
}

TEST(a_plus_sharing_a_level_with_other_text_is_rejected)
{
    /* "sport/tennis+" looks like it might mean "sport/tennis/+", but a
       wildcard only works alone in its level. */
    CHECK_EQ_INT(mqtt_check_subscribe_filter("sport/tennis+"),
                 MQTT_TOPIC_BAD_WILDCARD_PLACEMENT);
    CHECK_EQ_INT(mqtt_check_subscribe_filter("sport/+tennis"),
                 MQTT_TOPIC_BAD_WILDCARD_PLACEMENT);
}

TEST(a_hash_not_alone_in_its_level_is_rejected)
{
    CHECK_EQ_INT(mqtt_check_subscribe_filter("sport/tennis#"),
                 MQTT_TOPIC_BAD_WILDCARD_PLACEMENT);
}

TEST(a_hash_that_is_not_the_last_level_is_rejected)
{
    /* '#' matches everything below it, so anything typed after it could
       never be reached -- accepting this would silently discard part of
       the filter the caller wrote. */
    CHECK_EQ_INT(mqtt_check_subscribe_filter("robot/#/status"),
                 MQTT_TOPIC_BAD_WILDCARD_PLACEMENT);
}

TEST(an_empty_or_too_long_filter_is_rejected)
{
    CHECK_EQ_INT(mqtt_check_subscribe_filter(NULL), MQTT_TOPIC_EMPTY);
    CHECK_EQ_INT(mqtt_check_subscribe_filter(""), MQTT_TOPIC_EMPTY);

    char topic[MQTT_TOPIC_MAX_LENGTH + 2];
    memset(topic, 'a', sizeof(topic) - 1);
    topic[sizeof(topic) - 1] = '\0';
    CHECK_EQ_INT(mqtt_check_subscribe_filter(topic), MQTT_TOPIC_TOO_LONG);
}

/* ---------------------------------------------------------------------------
 * Client ids
 * -------------------------------------------------------------------------*/

TEST(a_reasonable_client_id_is_accepted)
{
    CHECK_EQ_INT(mqtt_check_client_id("robot-1"), MQTT_CLIENT_ID_OK);
}

TEST(an_empty_client_id_is_rejected)
{
    CHECK_EQ_INT(mqtt_check_client_id(NULL), MQTT_CLIENT_ID_EMPTY);
    CHECK_EQ_INT(mqtt_check_client_id(""), MQTT_CLIENT_ID_EMPTY);
}

TEST(a_client_id_over_the_limit_is_rejected)
{
    char id[MQTT_CLIENT_ID_MAX_LENGTH + 2];
    memset(id, 'a', sizeof(id) - 1);
    id[sizeof(id) - 1] = '\0';
    CHECK_EQ_INT(mqtt_check_client_id(id), MQTT_CLIENT_ID_TOO_LONG);

    id[MQTT_CLIENT_ID_MAX_LENGTH] = '\0';
    CHECK_EQ_INT(mqtt_check_client_id(id), MQTT_CLIENT_ID_OK);
}

/* ---------------------------------------------------------------------------
 * QoS
 * -------------------------------------------------------------------------*/

TEST(only_zero_one_and_two_are_valid_qos_levels)
{
    CHECK(mqtt_qos_is_valid(0));
    CHECK(mqtt_qos_is_valid(1));
    CHECK(mqtt_qos_is_valid(2));
    CHECK(!mqtt_qos_is_valid(3));
    CHECK(!mqtt_qos_is_valid(255));
}

TEST_MAIN(
    RUN(a_reasonable_publish_topic_is_accepted);
    RUN(an_empty_publish_topic_is_rejected);
    RUN(a_publish_topic_cannot_carry_a_wildcard);
    RUN(a_publish_topic_over_the_limit_is_rejected);

    RUN(plain_and_wildcard_filters_that_are_legal_are_accepted);
    RUN(a_plus_sharing_a_level_with_other_text_is_rejected);
    RUN(a_hash_not_alone_in_its_level_is_rejected);
    RUN(a_hash_that_is_not_the_last_level_is_rejected);
    RUN(an_empty_or_too_long_filter_is_rejected);

    RUN(a_reasonable_client_id_is_accepted);
    RUN(an_empty_client_id_is_rejected);
    RUN(a_client_id_over_the_limit_is_rejected);

    RUN(only_zero_one_and_two_are_valid_qos_levels);
)
