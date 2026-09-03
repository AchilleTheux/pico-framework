/*
 * Host-side tests for the home_led Home Assistant layer.
 *
 * This is the file that stands in for a Home Assistant instance. Everything
 * here is a payload that a real one sends or expects, so a schema mistake --
 * a colour mode contradicting its attribute, a partial command read as a whole
 * one, an effect name that no longer matches the published list -- is caught
 * here rather than as a light that "sometimes goes white on its own".
 */

#include "test.h"

#include <string.h>

#include "ha.h"
#include "json.h"
#include "light.h"

static void settle(light_t *light)
{
    light_tick(light, 100000u);
}

/* ---------------------------------------------------------------------------
 * Topics
 * -------------------------------------------------------------------------*/

TEST(topics_are_derived_from_the_device_id)
{
    ha_t ha;

    CHECK(ha_init(&ha, "pico1", NULL));
    CHECK_EQ_STR(ha.topic_config, "homeassistant/light/pico1/config");
    CHECK_EQ_STR(ha.topic_command, "homeassistant/light/pico1/set");
    CHECK_EQ_STR(ha.topic_state, "homeassistant/light/pico1/state");
    CHECK_EQ_STR(ha.topic_availability, "homeassistant/light/pico1/status");
}

TEST(a_custom_discovery_prefix_is_honoured)
{
    ha_t ha;

    CHECK(ha_init(&ha, "lamp", "ha"));
    CHECK_EQ_STR(ha.topic_config, "ha/light/lamp/config");
    CHECK_EQ_STR(ha.topic_command, "ha/light/lamp/set");
}

TEST(an_unusable_device_id_is_refused_rather_than_truncated)
{
    /* Half a topic is worse than none: the light would answer on one topic and
       be deaf on another, which looks like a broker problem. */
    ha_t ha;
    char too_long[HA_DEVICE_ID_MAX_LENGTH + 10u];

    memset(too_long, 'x', sizeof(too_long) - 1u);
    too_long[sizeof(too_long) - 1u] = '\0';

    CHECK(!ha_init(&ha, too_long, NULL));
    CHECK(!ha_init(&ha, "", NULL));
    CHECK(!ha_init(&ha, NULL, NULL));
    CHECK(!ha_init(NULL, "pico1", NULL));

    /* A prefix long enough to overflow the topic must be refused too. */
    char long_prefix[HA_TOPIC_MAX_LENGTH];
    memset(long_prefix, 'p', sizeof(long_prefix) - 1u);
    long_prefix[sizeof(long_prefix) - 1u] = '\0';
    CHECK(!ha_init(&ha, "pico1", long_prefix));
}

/* ---------------------------------------------------------------------------
 * Discovery
 * -------------------------------------------------------------------------*/

TEST(the_discovery_document_says_what_home_assistant_needs_to_know)
{
    ha_t ha;
    char payload[HA_DISCOVERY_BUFFER_SIZE];
    json_value_t value;
    json_value_t element;

    CHECK(ha_init(&ha, "pico1", NULL));

    const size_t length = ha_build_discovery(&ha, payload, sizeof(payload));
    CHECK(length > 0);
    CHECK(json_valid(payload));

    /* The schema that makes Home Assistant send whole JSON documents. */
    CHECK(json_find(payload, "schema", &value));
    CHECK(json_string_equals(&value, "json"));

    /* A stable unique_id is what stops it appearing as a new entity every
       time the firmware restarts. */
    CHECK(json_find(payload, "unique_id", &value));
    CHECK(json_string_equals(&value, "pico1_light"));

    CHECK(json_find(payload, "command_topic", &value));
    CHECK(json_string_equals(&value, ha.topic_command));
    CHECK(json_find(payload, "state_topic", &value));
    CHECK(json_string_equals(&value, ha.topic_state));
    CHECK(json_find(payload, "availability_topic", &value));
    CHECK(json_string_equals(&value, ha.topic_availability));

    CHECK(json_find(payload, "payload_available", &value));
    CHECK(json_string_equals(&value, HA_AVAILABLE));

    CHECK(json_find(payload, "min_mireds", &value));
    int32_t number;
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, (int)LIGHT_MIREDS_MIN);
    CHECK(json_find(payload, "max_mireds", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, (int)LIGHT_MIREDS_MAX);

    CHECK(json_find(payload, "supported_color_modes", &value));
    CHECK_EQ_INT((int)json_array_length(&value), 2);

    /* The device block groups the entity under one device across restarts. */
    CHECK(json_find(payload, "device", &value));
    CHECK(json_find_in(&value, "identifiers", &element));
    CHECK_EQ_INT((int)json_array_length(&element), 1);
}

TEST(the_published_effect_list_is_exactly_what_the_firmware_accepts)
{
    /*
     * The failure this prevents: Home Assistant offers a name in its dropdown
     * that light_effect_from_name() has never heard of, so selecting it does
     * nothing and there is no error anywhere to explain why.
     */
    ha_t ha;
    char payload[HA_DISCOVERY_BUFFER_SIZE];
    json_value_t list;

    CHECK(ha_init(&ha, "pico1", NULL));
    CHECK(ha_build_discovery(&ha, payload, sizeof(payload)) > 0);
    CHECK(json_find(payload, "effect_list", &list));
    CHECK_EQ_INT((int)json_array_length(&list), (int)LIGHT_EFFECT_COUNT);

    for (size_t i = 0; i < json_array_length(&list); i++) {
        json_value_t element;
        char name[40];
        light_effect_t effect;

        CHECK(json_array_at(&list, i, &element));
        CHECK(json_get_string(&element, name, sizeof(name)));
        CHECK(light_effect_from_name(name, &effect));
        CHECK_EQ_INT((int)effect, (int)i);
    }
}

TEST(a_discovery_buffer_that_is_too_small_reports_failure)
{
    /* Not a truncated document published as if it were whole. */
    ha_t ha;
    char payload[64];

    CHECK(ha_init(&ha, "pico1", NULL));
    CHECK_EQ_INT((int)ha_build_discovery(&ha, payload, sizeof(payload)), 0);
}

TEST(the_discovery_document_is_the_size_the_buffers_were_chosen_for)
{
    /*
     * Recorded because two other decisions depend on it: the 1 KiB buffer
     * here, and lwIP's MQTT_OUTPUT_RINGBUF_SIZE override, which exists solely
     * because this does not fit in the 256-byte default.
     */
    ha_t ha;
    char payload[HA_DISCOVERY_BUFFER_SIZE];

    CHECK(ha_init(&ha, "pico1", NULL));

    const size_t length = ha_build_discovery(&ha, payload, sizeof(payload));
    CHECK(length > 256u);
    CHECK(length < HA_DISCOVERY_BUFFER_SIZE);
}

/* ---------------------------------------------------------------------------
 * State
 * -------------------------------------------------------------------------*/

TEST(state_reports_rgb_mode_with_a_colour_and_no_temperature)
{
    /* Home Assistant treats an attribute that does not belong to the reported
       colour mode as a contradiction, so exactly one must be present. */
    ha_t ha;
    light_t light;
    char payload[HA_STATE_BUFFER_SIZE];
    json_value_t value;
    json_value_t channel;
    int32_t number;

    CHECK(ha_init(&ha, "pico1", NULL));
    light_init(&light, 0);
    light_set_color(&light, ws2812_rgb(10, 200, 30), 0);
    light_set_brightness(&light, 180, 0);
    settle(&light);

    CHECK(ha_build_state(&ha, &light, payload, sizeof(payload)) > 0);
    CHECK(json_valid(payload));

    CHECK(json_find(payload, "state", &value));
    CHECK(json_string_equals(&value, "ON"));
    CHECK(json_find(payload, "color_mode", &value));
    CHECK(json_string_equals(&value, "rgb"));

    CHECK(json_find(payload, "color", &value));
    CHECK(json_find_in(&value, "g", &channel));
    CHECK(json_get_int(&channel, &number));
    CHECK_EQ_INT(number, 200);

    CHECK(!json_find(payload, "color_temp", &value));
}

TEST(state_reports_temperature_mode_with_a_temperature_and_no_colour)
{
    ha_t ha;
    light_t light;
    char payload[HA_STATE_BUFFER_SIZE];
    json_value_t value;
    int32_t number;

    CHECK(ha_init(&ha, "pico1", NULL));
    light_init(&light, 0);
    light_set_mireds(&light, 370, 0);
    settle(&light);

    CHECK(ha_build_state(&ha, &light, payload, sizeof(payload)) > 0);

    CHECK(json_find(payload, "color_mode", &value));
    CHECK(json_string_equals(&value, "color_temp"));
    CHECK(json_find(payload, "color_temp", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 370);

    CHECK(!json_find(payload, "color", &value));
}

TEST(state_reports_what_was_asked_for_not_what_the_fade_has_reached)
{
    /*
     * A controller that just set the brightness to 200 must see 200. Reporting
     * the ramp would make its slider crawl for three seconds after every
     * change, and fight whatever the user did next.
     */
    ha_t ha;
    light_t light;
    char payload[HA_STATE_BUFFER_SIZE];
    json_value_t value;
    int32_t number;

    CHECK(ha_init(&ha, "pico1", NULL));
    light_init(&light, 0);
    light_set_brightness(&light, 10, 0);
    settle(&light);

    light_set_brightness(&light, 200, 100000u);
    light_tick(&light, 100001u);          /* barely started */

    CHECK(light_current_brightness(&light) < 20);

    CHECK(ha_build_state(&ha, &light, payload, sizeof(payload)) > 0);
    CHECK(json_find(payload, "brightness", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 200);
}

TEST(state_reports_off_without_losing_the_rest)
{
    /* Home Assistant keeps showing the colour and effect of a light that is
       off, so it can restore them; a bare {"state":"OFF"} loses that. */
    ha_t ha;
    light_t light;
    char payload[HA_STATE_BUFFER_SIZE];
    json_value_t value;

    CHECK(ha_init(&ha, "pico1", NULL));
    light_init(&light, 0);
    light_set_effect(&light, LIGHT_EFFECT_TWINKLE, 0);
    light_set_power(&light, false, 0);

    CHECK(ha_build_state(&ha, &light, payload, sizeof(payload)) > 0);
    CHECK(json_find(payload, "state", &value));
    CHECK(json_string_equals(&value, "OFF"));
    CHECK(json_find(payload, "effect", &value));
    CHECK(json_string_equals(&value, "Twinkle"));
    CHECK(json_find(payload, "brightness", &value));
}

TEST(a_state_buffer_that_is_too_small_reports_failure)
{
    ha_t ha;
    light_t light;
    char payload[16];

    CHECK(ha_init(&ha, "pico1", NULL));
    light_init(&light, 0);

    CHECK_EQ_INT((int)ha_build_state(&ha, &light, payload, sizeof(payload)), 0);
}

/* ---------------------------------------------------------------------------
 * Commands
 * -------------------------------------------------------------------------*/

static ha_command_t parse(const char *payload)
{
    ha_command_t command;

    memset(&command, 0, sizeof(command));
    (void)ha_parse_command(payload, strlen(payload), &command);
    return command;
}

TEST(a_full_command_is_read_field_by_field)
{
    const ha_command_t command = parse(
        "{\"state\":\"ON\",\"brightness\":180,"
        "\"color\":{\"r\":255,\"g\":128,\"b\":0},"
        "\"effect\":\"Wipe\"}");

    CHECK(command.has_power);
    CHECK(command.power);
    CHECK(command.has_brightness);
    CHECK_EQ_INT(command.brightness, 180);
    CHECK(command.has_color);
    CHECK_EQ_INT(command.color.r, 255);
    CHECK_EQ_INT(command.color.g, 128);
    CHECK_EQ_INT(command.color.b, 0);
    CHECK(command.has_effect);
    CHECK_EQ_INT((int)command.effect, (int)LIGHT_EFFECT_WIPE);
    CHECK(!command.unknown_effect);
}

TEST(a_partial_command_leaves_everything_else_absent)
{
    /*
     * The single most important behaviour here. Dragging a brightness slider
     * sends `{"brightness":140}` and nothing else; reading the missing fields
     * as zero would switch the light off and paint it black on every step of
     * the drag.
     */
    const ha_command_t command = parse("{\"brightness\":140}");

    CHECK(command.has_brightness);
    CHECK_EQ_INT(command.brightness, 140);
    CHECK(!command.has_power);
    CHECK(!command.has_color);
    CHECK(!command.has_mireds);
    CHECK(!command.has_effect);
}

TEST(off_is_recognised_and_is_not_just_the_absence_of_on)
{
    const ha_command_t off = parse("{\"state\":\"OFF\"}");
    const ha_command_t nonsense = parse("{\"state\":\"maybe\"}");

    CHECK(off.has_power);
    CHECK(!off.power);

    /* An unrecognised value is not silently taken as one or the other. */
    CHECK(!nonsense.has_power);
}

TEST(a_colour_temperature_command_is_read)
{
    const ha_command_t command = parse("{\"color_temp\":370}");

    CHECK(command.has_mireds);
    CHECK_EQ_INT(command.mireds, 370);
    CHECK(!command.has_color);
}

TEST(a_colour_needs_all_three_channels)
{
    /* Two channels is not a colour, and filling the third in with zero would
       be inventing one. */
    const ha_command_t partial = parse("{\"color\":{\"r\":10,\"g\":20}}");
    const ha_command_t complete = parse("{\"color\":{\"r\":10,\"g\":20,\"b\":30}}");

    CHECK(!partial.has_color);
    CHECK(complete.has_color);
    CHECK_EQ_INT(complete.color.b, 30);
}

TEST(out_of_range_numbers_are_clamped_into_the_light_s_range)
{
    const ha_command_t command = parse(
        "{\"brightness\":400,\"color\":{\"r\":-5,\"g\":999,\"b\":30}}");

    CHECK(command.has_brightness);
    CHECK_EQ_INT(command.brightness, 255);
    CHECK(command.has_color);
    CHECK_EQ_INT(command.color.r, 0);
    CHECK_EQ_INT(command.color.g, 255);
}

TEST(an_effect_name_this_firmware_does_not_have_is_reported_not_ignored)
{
    /* It almost always means the published effect list and the code have
       drifted apart, which is worth a log line rather than silence. */
    const ha_command_t command = parse("{\"effect\":\"Strobe\"}");

    CHECK(!command.has_effect);
    CHECK(command.unknown_effect);
}

TEST(the_key_inside_a_value_trap)
{
    /*
     * The reason this uses the json component instead of a substring search.
     * An effect named "brightness test" must not be read as a brightness, and
     * the nested colour channels must not be found at the top level.
     */
    const ha_command_t named = parse("{\"effect\":\"brightness test\"}");
    const ha_command_t nested = parse("{\"color\":{\"r\":1,\"g\":2,\"b\":3}}");

    CHECK(!named.has_brightness);
    CHECK(named.unknown_effect);        /* not an effect this device offers */

    CHECK(nested.has_color);
    CHECK(!nested.has_brightness);
}

TEST(a_payload_that_is_not_a_command_is_refused)
{
    ha_command_t command;

    CHECK(!ha_parse_command("[1,2,3]", 7, &command));
    CHECK(!ha_parse_command("42", 2, &command));
    CHECK(!ha_parse_command("\"ON\"", 4, &command));
    CHECK(!ha_parse_command("", 0, &command));
    CHECK(!ha_parse_command(NULL, 4, &command));
    CHECK(!ha_parse_command("not json at all", 15, &command));

    /* Cut short in transit: nothing in it may be believed. */
    CHECK(!ha_parse_command("{\"brightness\":42", 16, &command));
}

TEST(a_valid_but_empty_command_is_accepted_and_asks_for_nothing)
{
    ha_command_t command;

    CHECK(ha_parse_command("{}", 2, &command));
    CHECK(!command.has_power);
    CHECK(!command.has_brightness);
    CHECK(!command.has_effect);
}

TEST(a_payload_is_read_within_its_length_not_to_a_terminator)
{
    /* An MQTT payload is a pointer and a length; the bytes after it belong to
       whatever lwIP had in that buffer. */
    ha_command_t command;
    const char buffer[] = "{\"brightness\":7}{\"brightness\":9}";

    CHECK(ha_parse_command(buffer, 16, &command));
    CHECK(command.has_brightness);
    CHECK_EQ_INT(command.brightness, 7);
}

/* ---------------------------------------------------------------------------
 * Applying
 * -------------------------------------------------------------------------*/

TEST(applying_a_command_moves_the_light)
{
    light_t light;
    const ha_command_t command = parse(
        "{\"state\":\"ON\",\"brightness\":90,\"color\":{\"r\":1,\"g\":2,\"b\":3},"
        "\"effect\":\"Rainbow\"}");

    light_init(&light, 0);
    light_set_power(&light, false, 0);

    ha_apply_command(&command, &light, 0);
    settle(&light);

    CHECK(light.on);
    CHECK_EQ_INT(light.brightness, 90);
    CHECK_EQ_INT(light.color.g, 2);
    CHECK_EQ_INT((int)light.effect, (int)LIGHT_EFFECT_RAINBOW);
    CHECK_EQ_INT((int)light.color_mode, (int)LIGHT_COLOR_MODE_RGB);
}

TEST(a_command_that_asks_for_nothing_changes_nothing)
{
    light_t light;
    ha_command_t command;

    light_init(&light, 0);
    settle(&light);
    const uint32_t generation = light.generation;

    CHECK(ha_parse_command("{}", 2, &command));
    ha_apply_command(&command, &light, 0);

    CHECK_EQ_INT((int)light.generation, (int)generation);
}

TEST(a_command_survives_the_round_trip_through_the_state_it_produces)
{
    /*
     * Whatever this device reports, it must be able to read back. Home
     * Assistant restores a light by sending its last known state, so a state
     * document this parser cannot understand is a light that comes back wrong.
     */
    ha_t ha;
    light_t light;
    light_t restored;
    char payload[HA_STATE_BUFFER_SIZE];
    ha_command_t command;

    CHECK(ha_init(&ha, "pico1", NULL));

    light_init(&light, 0);
    light_set_color(&light, ws2812_rgb(7, 200, 64), 0);
    light_set_brightness(&light, 173, 0);
    light_set_effect(&light, LIGHT_EFFECT_BREATHING, 0);
    settle(&light);

    const size_t length = ha_build_state(&ha, &light, payload, sizeof(payload));
    CHECK(length > 0);

    CHECK(ha_parse_command(payload, length, &command));
    light_init(&restored, 0);
    ha_apply_command(&command, &restored, 0);
    settle(&restored);

    CHECK_EQ_INT(restored.brightness, light.brightness);
    CHECK_EQ_INT(restored.color.r, light.color.r);
    CHECK_EQ_INT(restored.color.g, light.color.g);
    CHECK_EQ_INT(restored.color.b, light.color.b);
    CHECK_EQ_INT((int)restored.effect, (int)light.effect);
    CHECK_EQ_INT((int)restored.color_mode, (int)light.color_mode);
    CHECK(restored.on == light.on);
}

TEST(a_colour_temperature_state_also_round_trips)
{
    ha_t ha;
    light_t light;
    light_t restored;
    char payload[HA_STATE_BUFFER_SIZE];
    ha_command_t command;

    CHECK(ha_init(&ha, "pico1", NULL));

    light_init(&light, 0);
    light_set_mireds(&light, 412, 0);
    settle(&light);

    const size_t length = ha_build_state(&ha, &light, payload, sizeof(payload));
    CHECK(ha_parse_command(payload, length, &command));

    light_init(&restored, 0);
    ha_apply_command(&command, &restored, 0);

    CHECK_EQ_INT((int)restored.color_mode, (int)LIGHT_COLOR_MODE_TEMP);
    CHECK_EQ_INT(restored.mireds, 412);
}

TEST(applying_to_nothing_is_survivable)
{
    ha_command_t command;
    light_t light;

    light_init(&light, 0);
    memset(&command, 0, sizeof(command));

    ha_apply_command(NULL, &light, 0);
    ha_apply_command(&command, NULL, 0);

    CHECK(ha_build_discovery(NULL, NULL, 0) == 0);
    CHECK(ha_build_state(NULL, NULL, NULL, 0) == 0);
}

TEST_MAIN(
    RUN(topics_are_derived_from_the_device_id);
    RUN(a_custom_discovery_prefix_is_honoured);
    RUN(an_unusable_device_id_is_refused_rather_than_truncated);
    RUN(the_discovery_document_says_what_home_assistant_needs_to_know);
    RUN(the_published_effect_list_is_exactly_what_the_firmware_accepts);
    RUN(a_discovery_buffer_that_is_too_small_reports_failure);
    RUN(the_discovery_document_is_the_size_the_buffers_were_chosen_for);
    RUN(state_reports_rgb_mode_with_a_colour_and_no_temperature);
    RUN(state_reports_temperature_mode_with_a_temperature_and_no_colour);
    RUN(state_reports_what_was_asked_for_not_what_the_fade_has_reached);
    RUN(state_reports_off_without_losing_the_rest);
    RUN(a_state_buffer_that_is_too_small_reports_failure);
    RUN(a_full_command_is_read_field_by_field);
    RUN(a_partial_command_leaves_everything_else_absent);
    RUN(off_is_recognised_and_is_not_just_the_absence_of_on);
    RUN(a_colour_temperature_command_is_read);
    RUN(a_colour_needs_all_three_channels);
    RUN(out_of_range_numbers_are_clamped_into_the_light_s_range);
    RUN(an_effect_name_this_firmware_does_not_have_is_reported_not_ignored);
    RUN(the_key_inside_a_value_trap);
    RUN(a_payload_that_is_not_a_command_is_refused);
    RUN(a_valid_but_empty_command_is_accepted_and_asks_for_nothing);
    RUN(a_payload_is_read_within_its_length_not_to_a_terminator);
    RUN(applying_a_command_moves_the_light);
    RUN(a_command_that_asks_for_nothing_changes_nothing);
    RUN(a_command_survives_the_round_trip_through_the_state_it_produces);
    RUN(a_colour_temperature_state_also_round_trips);
    RUN(applying_to_nothing_is_survivable);
)
