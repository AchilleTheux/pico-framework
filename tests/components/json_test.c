/*
 * Host-side tests for the json component.
 *
 * The group that matters most is `strstr_traps`: those four cases are the
 * reason this component exists rather than a two-line search, and each of
 * them is a payload shape that turns up in practice rather than a contrived
 * one. A rewrite that passed everything else and failed those would have lost
 * the point.
 */

#include "test.h"

#include <string.h>

#include "json.h"

/* ---------------------------------------------------------------------------
 * What a naive search gets wrong
 * -------------------------------------------------------------------------*/

TEST(strstr_trap_key_appearing_inside_a_value)
{
    /* Home Assistant sends effect names chosen by the user. One of them
       containing the word "brightness" must not be read as a brightness. */
    const char *json = "{\"effect\":\"brightness test\"}";
    json_value_t value;

    CHECK(!json_find(json, "brightness", &value));
    CHECK_EQ_INT(value.type, JSON_TYPE_INVALID);

    CHECK(json_find(json, "effect", &value));
    CHECK(json_string_equals(&value, "brightness test"));
}

TEST(strstr_trap_key_nested_in_a_sub_object)
{
    /* "r" belongs to "color", not to the message. Reading it at the top level
       would apply a colour channel as if it were a top-level field. */
    const char *json = "{\"state\":\"ON\",\"color\":{\"r\":10,\"g\":20,\"b\":30}}";
    json_value_t colour;
    json_value_t channel;
    int32_t red = -1;

    CHECK(!json_find(json, "r", &channel));

    CHECK(json_find(json, "color", &colour));
    CHECK_EQ_INT(colour.type, JSON_TYPE_OBJECT);

    CHECK(json_find_in(&colour, "r", &channel));
    CHECK(json_get_int(&channel, &red));
    CHECK_EQ_INT(red, 10);
}

TEST(strstr_trap_one_key_is_a_prefix_of_another)
{
    const char *json = "{\"brightness_scale\":100,\"brightness\":42}";
    json_value_t value;
    int32_t number = -1;

    CHECK(json_find(json, "brightness", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 42);

    CHECK(json_find(json, "brightness_scale", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 100);
}

TEST(strstr_trap_key_name_only_present_as_a_prefix)
{
    /* Nothing here is a "bright" field, even though the text contains one. */
    const char *json = "{\"brightness\":42}";
    json_value_t value;

    CHECK(!json_find(json, "bright", &value));
    CHECK(!json_find(json, "ness", &value));
}

TEST(a_key_in_a_nested_object_does_not_shadow_the_outer_one)
{
    /* The outer "state" must win, whichever order they appear in. */
    const char *json = "{\"device\":{\"state\":\"OFF\"},\"state\":\"ON\"}";
    json_value_t value;

    CHECK(json_find(json, "state", &value));
    CHECK(json_string_equals(&value, "ON"));
}

/* ---------------------------------------------------------------------------
 * Locating values
 * -------------------------------------------------------------------------*/

TEST(find_reports_the_type_of_each_value)
{
    const char *json =
        "{\"s\":\"text\",\"n\":12,\"b\":true,\"z\":null,\"o\":{},\"a\":[]}";
    json_value_t value;

    CHECK(json_find(json, "s", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_STRING);
    CHECK(json_find(json, "n", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_NUMBER);
    CHECK(json_find(json, "b", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_BOOL);
    CHECK(json_find(json, "z", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_NULL);
    CHECK(json_find(json, "o", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_OBJECT);
    CHECK(json_find(json, "a", &value)); CHECK_EQ_INT(value.type, JSON_TYPE_ARRAY);
}

TEST(find_tolerates_whitespace_everywhere_it_is_allowed)
{
    const char *json = "  {\n  \"a\" :\t 1 ,\r\n  \"b\" : 2\n}  ";
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find(json, "b", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 2);
}

TEST(find_steps_over_nested_containers_to_reach_a_later_key)
{
    const char *json =
        "{\"a\":{\"deep\":[1,2,{\"x\":[3,4]}]},\"wanted\":7}";
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find(json, "wanted", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 7);
}

TEST(find_steps_over_strings_containing_structural_characters)
{
    /* Braces, brackets, colons and escaped quotes inside a value must not be
       taken for structure -- this is where a hand-rolled scanner usually
       comes apart. */
    const char *json =
        "{\"tricky\":\"}{,:[] \\\" \\\\ end\",\"wanted\":5}";
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find(json, "wanted", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 5);

    CHECK(json_find(json, "tricky", &value));
    CHECK(json_string_equals(&value, "}{,:[] \" \\ end"));
}

TEST(find_returns_the_object_body_so_it_can_be_searched_again)
{
    const char *json = "{\"outer\":{\"inner\":{\"leaf\":9}}}";
    json_value_t outer;
    json_value_t inner;
    json_value_t leaf;
    int32_t number = 0;

    CHECK(json_find(json, "outer", &outer));
    CHECK(json_find_in(&outer, "inner", &inner));
    CHECK(json_find_in(&inner, "leaf", &leaf));
    CHECK(json_get_int(&leaf, &number));
    CHECK_EQ_INT(number, 9);
}

TEST(find_rejects_things_that_are_not_objects)
{
    json_value_t value;

    CHECK(!json_find("[1,2,3]", "a", &value));
    CHECK(!json_find("\"text\"", "a", &value));
    CHECK(!json_find("42", "a", &value));
    CHECK(!json_find("{}", "a", &value));
    CHECK(!json_find("", "a", &value));
    CHECK(!json_find(NULL, "a", &value));
}

TEST(find_rejects_malformed_input_rather_than_guessing)
{
    json_value_t value;

    CHECK(!json_find("{\"a\":}", "a", &value));
    CHECK(!json_find("{\"a\" 1}", "a", &value));
    CHECK(!json_find("{\"a\":1", "a", &value));
    CHECK(!json_find("{\"a\":\"unterminated}", "a", &value));
    CHECK(!json_find("{a:1}", "a", &value));
}

TEST(find_over_a_buffer_that_is_not_zero_terminated)
{
    /* An MQTT payload arrives as a pointer and a length, so this is the shape
       a caller actually has. The trailing bytes must not be read. */
    const char buffer[] = "{\"a\":1}GARBAGE";
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find_n(buffer, 7, "a", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 1);
}

TEST(nesting_deeper_than_the_limit_is_refused)
{
    /* Reachable from anything that parses a message off the network, so it
       has to fail rather than run the stack out. */
    char json[512];
    size_t at = 0;

    for (unsigned i = 0; i < JSON_MAX_DEPTH + 4u; i++) {
        json[at++] = '[';
    }
    for (unsigned i = 0; i < JSON_MAX_DEPTH + 4u; i++) {
        json[at++] = ']';
    }
    json[at] = '\0';

    CHECK(!json_valid(json));
}

/* ---------------------------------------------------------------------------
 * Converting values
 * -------------------------------------------------------------------------*/

TEST(integers_round_trip_including_the_extremes)
{
    const struct {
        const char *json;
        int32_t expected;
    } cases[] = {
        { "{\"v\":0}",            0 },
        { "{\"v\":-0}",           0 },
        { "{\"v\":42}",           42 },
        { "{\"v\":-42}",          -42 },
        { "{\"v\":2147483647}",   2147483647 },
        { "{\"v\":-2147483648}",  (-2147483647 - 1) },
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        json_value_t value;
        int32_t number = 12345;

        CHECK(json_find(cases[i].json, "v", &value));
        CHECK(json_get_int(&value, &number));
        CHECK_EQ_INT(number, cases[i].expected);
    }
}

TEST(integers_out_of_range_are_refused_rather_than_wrapped)
{
    /* A wrapped brightness would be applied as though it were real. */
    const char *cases[] = {
        "{\"v\":2147483648}",
        "{\"v\":-2147483649}",
        "{\"v\":99999999999999}",
    };

    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        json_value_t value;
        int32_t number = 0;

        CHECK(json_find(cases[i], "v", &value));
        CHECK(!json_get_int(&value, &number));
    }
}

TEST(a_fractional_number_truncates_toward_zero)
{
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find("{\"v\":12.9}", "v", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 12);

    CHECK(json_find("{\"v\":-12.9}", "v", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, -12);
}

TEST(an_exponent_is_refused_rather_than_silently_rescaled)
{
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find("{\"v\":1e3}", "v", &value));
    CHECK(!json_get_int(&value, &number));
}

TEST(get_int_refuses_values_that_are_not_numbers)
{
    json_value_t value;
    int32_t number = 0;

    CHECK(json_find("{\"v\":\"42\"}", "v", &value));
    CHECK(!json_get_int(&value, &number));

    CHECK(json_find("{\"v\":true}", "v", &value));
    CHECK(!json_get_int(&value, &number));
}

TEST(booleans_read_only_from_true_and_false)
{
    json_value_t value;
    bool flag = false;

    CHECK(json_find("{\"v\":true}", "v", &value));
    CHECK(json_get_bool(&value, &flag));
    CHECK(flag);

    CHECK(json_find("{\"v\":false}", "v", &value));
    CHECK(json_get_bool(&value, &flag));
    CHECK(!flag);

    /* 1 and "on" are a number and a string; taking either as a boolean would
       be this component inventing a convention. */
    CHECK(json_find("{\"v\":1}", "v", &value));
    CHECK(!json_get_bool(&value, &flag));

    CHECK(json_find("{\"v\":\"on\"}", "v", &value));
    CHECK(!json_get_bool(&value, &flag));
}

TEST(strings_come_out_unescaped_and_terminated)
{
    json_value_t value;
    char out[64];

    CHECK(json_find("{\"v\":\"a\\\"b\\\\c\\nd\\te\\/f\"}", "v", &value));
    CHECK(json_get_string(&value, out, sizeof(out)));
    CHECK_EQ_STR(out, "a\"b\\c\nd\te/f");
}

TEST(a_unicode_escape_resolves_inside_ascii_and_is_refused_above_it)
{
    json_value_t value;
    char out[32];

    CHECK(json_find("{\"v\":\"\\u0041\\u007a\"}", "v", &value));
    CHECK(json_get_string(&value, out, sizeof(out)));
    CHECK_EQ_STR(out, "Az");

    /* Guessing an encoding for the caller is not this component's business. */
    CHECK(json_find("{\"v\":\"\\u00e9\"}", "v", &value));
    CHECK(!json_get_string(&value, out, sizeof(out)));
}

TEST(a_string_too_long_for_the_buffer_is_refused_not_truncated)
{
    /* A half-copied effect name matches nothing anyway, and truncating turns
       a bug into a mystery. */
    json_value_t value;
    char out[4];

    CHECK(json_find("{\"v\":\"abcdefgh\"}", "v", &value));
    CHECK(!json_get_string(&value, out, sizeof(out)));

    CHECK(json_find("{\"v\":\"abc\"}", "v", &value));
    CHECK(json_get_string(&value, out, sizeof(out)));
    CHECK_EQ_STR(out, "abc");
}

TEST(an_empty_string_is_a_string)
{
    json_value_t value;
    char out[8] = "dirty";

    CHECK(json_find("{\"v\":\"\"}", "v", &value));
    CHECK(json_get_string(&value, out, sizeof(out)));
    CHECK_EQ_STR(out, "");
    CHECK(json_string_equals(&value, ""));
}

TEST(string_equals_compares_after_unescaping_and_never_partially)
{
    json_value_t value;

    CHECK(json_find("{\"v\":\"Rain\\\"bow\"}", "v", &value));
    CHECK(json_string_equals(&value, "Rain\"bow"));
    CHECK(!json_string_equals(&value, "Rain"));
    CHECK(!json_string_equals(&value, "Rain\"bowing"));
    CHECK(!json_string_equals(&value, ""));
}

/* ---------------------------------------------------------------------------
 * Arrays
 * -------------------------------------------------------------------------*/

TEST(array_ints_reads_an_rgb_triple_in_one_call)
{
    /* The exact shape Home Assistant sends for a colour. */
    const char *json = "{\"rgb_color\":[255,128,0]}";
    json_value_t array;
    int32_t rgb[3] = { -1, -1, -1 };

    CHECK(json_find(json, "rgb_color", &array));
    CHECK_EQ_INT((int)json_array_length(&array), 3);
    CHECK_EQ_INT((int)json_array_ints(&array, rgb, 3), 3);
    CHECK_EQ_INT(rgb[0], 255);
    CHECK_EQ_INT(rgb[1], 128);
    CHECK_EQ_INT(rgb[2], 0);
}

TEST(array_ints_reports_how_many_it_managed)
{
    json_value_t array;
    int32_t out[4] = { -1, -1, -1, -1 };

    CHECK(json_find("{\"a\":[1,2]}", "a", &array));
    CHECK_EQ_INT((int)json_array_ints(&array, out, 4), 2);
    CHECK_EQ_INT(out[0], 1);
    CHECK_EQ_INT(out[1], 2);

    /* Stops at the first element that is not a number rather than skipping
       it, so a caller cannot silently get a shifted triple. */
    CHECK(json_find("{\"a\":[1,\"x\",3]}", "a", &array));
    CHECK_EQ_INT((int)json_array_ints(&array, out, 3), 1);
}

TEST(array_indexing_covers_mixed_and_nested_elements)
{
    const char *json = "{\"a\":[1,\"two\",{\"k\":3},[4,5],null]}";
    json_value_t array;
    json_value_t element;

    CHECK(json_find(json, "a", &array));
    CHECK_EQ_INT((int)json_array_length(&array), 5);

    CHECK(json_array_at(&array, 1, &element));
    CHECK(json_string_equals(&element, "two"));

    CHECK(json_array_at(&array, 2, &element));
    CHECK_EQ_INT(element.type, JSON_TYPE_OBJECT);

    json_value_t nested;
    int32_t number = 0;
    CHECK(json_find_in(&element, "k", &nested));
    CHECK(json_get_int(&nested, &number));
    CHECK_EQ_INT(number, 3);

    CHECK(json_array_at(&array, 3, &element));
    CHECK_EQ_INT(element.type, JSON_TYPE_ARRAY);
    CHECK_EQ_INT((int)json_array_length(&element), 2);

    CHECK(json_array_at(&array, 4, &element));
    CHECK_EQ_INT(element.type, JSON_TYPE_NULL);

    CHECK(!json_array_at(&array, 5, &element));
}

TEST(an_empty_array_has_no_elements)
{
    json_value_t array;
    json_value_t element;

    CHECK(json_find("{\"a\":[]}", "a", &array));
    CHECK_EQ_INT((int)json_array_length(&array), 0);
    CHECK(!json_array_at(&array, 0, &element));
}

/* ---------------------------------------------------------------------------
 * Validation
 * -------------------------------------------------------------------------*/

TEST(valid_accepts_whole_documents_only)
{
    CHECK(json_valid("{}"));
    CHECK(json_valid("  {\"a\":[1,2,{\"b\":null}]}  "));
    CHECK(json_valid("[]"));
    CHECK(json_valid("42"));
    CHECK(json_valid("\"text\""));

    /* Trailing content means the sender and this parser disagree about where
       the document ends, which is worth reporting rather than ignoring. */
    CHECK(!json_valid("{} {}"));
    CHECK(!json_valid("{}x"));
    CHECK(!json_valid("{"));
    CHECK(!json_valid("}"));
    CHECK(!json_valid(""));
    CHECK(!json_valid("{\"a\":1,}"));
    CHECK(!json_valid("[1,2,]"));
    CHECK(!json_valid("tru"));
    CHECK(!json_valid("-"));
    CHECK(!json_valid(NULL));
}

/* ---------------------------------------------------------------------------
 * Writing
 * -------------------------------------------------------------------------*/

TEST(writer_builds_a_flat_object)
{
    char buffer[128];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "state", "ON");
    json_writer_int(&writer, "brightness", 200);
    json_writer_bool(&writer, "on", true);
    json_writer_null(&writer, "nothing");
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK_EQ_STR(buffer,
        "{\"state\":\"ON\",\"brightness\":200,\"on\":true,\"nothing\":null}");
    CHECK_EQ_INT((int)json_writer_length(&writer), (int)strlen(buffer));
}

TEST(writer_nests_objects_and_arrays_with_correct_commas)
{
    char buffer[256];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "name", "Pico LED");
    json_writer_array_open(&writer, "rgb");
    json_writer_int(&writer, NULL, 255);
    json_writer_int(&writer, NULL, 128);
    json_writer_int(&writer, NULL, 0);
    json_writer_array_close(&writer);
    json_writer_object_open(&writer, "device");
    json_writer_string(&writer, "model", "MQTT LED");
    json_writer_object_close(&writer);
    json_writer_int(&writer, "qos", 1);
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK_EQ_STR(buffer,
        "{\"name\":\"Pico LED\",\"rgb\":[255,128,0],"
        "\"device\":{\"model\":\"MQTT LED\"},\"qos\":1}");
}

TEST(what_the_writer_produces_is_readable_by_the_scanner)
{
    /* The two halves must agree, or a device cannot read back its own state. */
    char buffer[128];
    json_writer_t writer;
    json_value_t value;
    int32_t number = 0;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "effect", "Rain\"bow\\slash");
    json_writer_int(&writer, "brightness", -7);
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK(json_valid(buffer));

    CHECK(json_find(buffer, "effect", &value));
    CHECK(json_string_equals(&value, "Rain\"bow\\slash"));

    CHECK(json_find(buffer, "brightness", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, -7);
}

TEST(writer_escapes_control_characters)
{
    char buffer[64];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "v", "a\nb\x01");
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK_EQ_STR(buffer, "{\"v\":\"a\\nb\\u0001\"}");
    CHECK(json_valid(buffer));
}

TEST(writer_writes_the_int32_extremes)
{
    char buffer[64];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_array_open(&writer, NULL);
    json_writer_int(&writer, NULL, -2147483647 - 1);
    json_writer_int(&writer, NULL, 2147483647);
    json_writer_int(&writer, NULL, 0);
    json_writer_array_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK_EQ_STR(buffer, "[-2147483648,2147483647,0]");
}

TEST(writer_inserts_raw_fragments_verbatim)
{
    char buffer[96];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_raw(&writer, "supported_color_modes", "[\"rgb\",\"color_temp\"]");
    json_writer_int(&writer, "min_mireds", 153);
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK_EQ_STR(buffer,
        "{\"supported_color_modes\":[\"rgb\",\"color_temp\"],\"min_mireds\":153}");
}

TEST(a_document_that_does_not_fit_is_reported_not_silently_truncated)
{
    /* The failure this whole "no return values, one check at the end" design
       exists to catch. */
    char buffer[16];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "a_long_key_name", "a_long_value");
    json_writer_object_close(&writer);

    CHECK(!json_writer_finish(&writer));

    /* Still terminated, so a caller can log how far it got. */
    CHECK(strlen(buffer) < sizeof(buffer));
}

TEST(an_unbalanced_document_is_reported)
{
    char buffer[64];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_int(&writer, "a", 1);
    /* No close. */
    CHECK(!json_writer_finish(&writer));

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_object_close(&writer);
    json_writer_object_close(&writer);   /* one too many */
    CHECK(!json_writer_finish(&writer));
}

TEST(a_zero_sized_buffer_fails_rather_than_writing)
{
    char buffer[1] = { 'x' };
    json_writer_t writer;

    json_writer_init(&writer, buffer, 0);
    json_writer_object_open(&writer, NULL);
    json_writer_object_close(&writer);

    CHECK(!json_writer_finish(&writer));
    CHECK_EQ_INT(buffer[0], 'x');
}

TEST(writer_survives_a_null_buffer)
{
    json_writer_t writer;

    json_writer_init(&writer, NULL, 0);
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "a", "b");
    json_writer_object_close(&writer);

    CHECK(!json_writer_finish(&writer));
}

TEST(a_discovery_sized_document_fits_and_reads_back)
{
    /*
     * The real target: roughly what a Home Assistant light announces about
     * itself. Both that it builds and that its length is what forced
     * MQTT_OUTPUT_RINGBUF_SIZE up from lwIP's 256-byte default.
     */
    static const char *const effects[] = {
        "Solid", "Rainbow", "Twinkle", "Wipe", "Breathing"
    };
    char buffer[1024];
    json_writer_t writer;

    json_writer_init(&writer, buffer, sizeof(buffer));
    json_writer_object_open(&writer, NULL);
    json_writer_string(&writer, "name", "Pico LED");
    json_writer_string(&writer, "uniq_id", "pico1_led");
    json_writer_string(&writer, "schema", "json");
    json_writer_string(&writer, "cmd_t", "homeassistant/light/pico1/set");
    json_writer_string(&writer, "stat_t", "homeassistant/light/pico1/state");
    json_writer_string(&writer, "avty_t", "homeassistant/light/pico1/status");
    json_writer_bool(&writer, "brightness", true);
    json_writer_array_open(&writer, "supported_color_modes");
    json_writer_string(&writer, NULL, "rgb");
    json_writer_string(&writer, NULL, "color_temp");
    json_writer_array_close(&writer);
    json_writer_int(&writer, "min_mireds", 153);
    json_writer_int(&writer, "max_mireds", 500);
    json_writer_bool(&writer, "effect", true);
    json_writer_array_open(&writer, "effect_list");
    for (size_t i = 0; i < sizeof(effects) / sizeof(effects[0]); i++) {
        json_writer_string(&writer, NULL, effects[i]);
    }
    json_writer_array_close(&writer);
    json_writer_int(&writer, "qos", 1);
    json_writer_object_open(&writer, "device");
    json_writer_array_open(&writer, "identifiers");
    json_writer_string(&writer, NULL, "pico1");
    json_writer_array_close(&writer);
    json_writer_string(&writer, "name", "Pico Controller");
    json_writer_string(&writer, "manufacturer", "Raspberry Pi");
    json_writer_string(&writer, "model", "Pico 2 W");
    json_writer_object_close(&writer);
    json_writer_object_close(&writer);

    CHECK(json_writer_finish(&writer));
    CHECK(json_valid(buffer));

    /* Comfortably past lwIP's default output ring buffer. */
    CHECK(json_writer_length(&writer) > 256u);

    json_value_t value;
    json_value_t element;
    CHECK(json_find(buffer, "effect_list", &value));
    CHECK_EQ_INT((int)json_array_length(&value), 5);
    CHECK(json_array_at(&value, 4, &element));
    CHECK(json_string_equals(&element, "Breathing"));

    CHECK(json_find(buffer, "device", &value));
    CHECK(json_find_in(&value, "model", &element));
    CHECK(json_string_equals(&element, "Pico 2 W"));

    /* "name" appears both at the top level and inside "device"; the outer one
       must be the one a top-level search finds. */
    CHECK(json_find(buffer, "name", &value));
    CHECK(json_string_equals(&value, "Pico LED"));
}

/* ---------------------------------------------------------------------------
 * A whole Home Assistant command
 * -------------------------------------------------------------------------*/

TEST(a_home_assistant_light_command_parses_field_by_field)
{
    const char *payload =
        "{\"state\":\"ON\",\"brightness\":180,"
        "\"color\":{\"r\":255,\"g\":128,\"b\":0},"
        "\"color_temp\":250,\"effect\":\"Twinkle\"}";
    json_value_t value;
    json_value_t colour;
    int32_t number = 0;
    int32_t rgb[3] = { 0, 0, 0 };

    CHECK(json_find(payload, "state", &value));
    CHECK(json_string_equals(&value, "ON"));

    CHECK(json_find(payload, "brightness", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 180);

    CHECK(json_find(payload, "color", &colour));
    CHECK(json_find_in(&colour, "r", &value));
    CHECK(json_get_int(&value, &rgb[0]));
    CHECK(json_find_in(&colour, "g", &value));
    CHECK(json_get_int(&value, &rgb[1]));
    CHECK(json_find_in(&colour, "b", &value));
    CHECK(json_get_int(&value, &rgb[2]));
    CHECK_EQ_INT(rgb[0], 255);
    CHECK_EQ_INT(rgb[1], 128);
    CHECK_EQ_INT(rgb[2], 0);

    CHECK(json_find(payload, "color_temp", &value));
    CHECK(json_get_int(&value, &number));
    CHECK_EQ_INT(number, 250);

    CHECK(json_find(payload, "effect", &value));
    CHECK(json_string_equals(&value, "Twinkle"));

    /* Absent fields simply are not there, which is how a partial command --
       the usual case from Home Assistant -- gets applied. */
    CHECK(!json_find(payload, "white", &value));
    CHECK(!json_find(payload, "transition", &value));
}

TEST_MAIN(
    RUN(strstr_trap_key_appearing_inside_a_value);
    RUN(strstr_trap_key_nested_in_a_sub_object);
    RUN(strstr_trap_one_key_is_a_prefix_of_another);
    RUN(strstr_trap_key_name_only_present_as_a_prefix);
    RUN(a_key_in_a_nested_object_does_not_shadow_the_outer_one);
    RUN(find_reports_the_type_of_each_value);
    RUN(find_tolerates_whitespace_everywhere_it_is_allowed);
    RUN(find_steps_over_nested_containers_to_reach_a_later_key);
    RUN(find_steps_over_strings_containing_structural_characters);
    RUN(find_returns_the_object_body_so_it_can_be_searched_again);
    RUN(find_rejects_things_that_are_not_objects);
    RUN(find_rejects_malformed_input_rather_than_guessing);
    RUN(find_over_a_buffer_that_is_not_zero_terminated);
    RUN(nesting_deeper_than_the_limit_is_refused);
    RUN(integers_round_trip_including_the_extremes);
    RUN(integers_out_of_range_are_refused_rather_than_wrapped);
    RUN(a_fractional_number_truncates_toward_zero);
    RUN(an_exponent_is_refused_rather_than_silently_rescaled);
    RUN(get_int_refuses_values_that_are_not_numbers);
    RUN(booleans_read_only_from_true_and_false);
    RUN(strings_come_out_unescaped_and_terminated);
    RUN(a_unicode_escape_resolves_inside_ascii_and_is_refused_above_it);
    RUN(a_string_too_long_for_the_buffer_is_refused_not_truncated);
    RUN(an_empty_string_is_a_string);
    RUN(string_equals_compares_after_unescaping_and_never_partially);
    RUN(array_ints_reads_an_rgb_triple_in_one_call);
    RUN(array_ints_reports_how_many_it_managed);
    RUN(array_indexing_covers_mixed_and_nested_elements);
    RUN(an_empty_array_has_no_elements);
    RUN(valid_accepts_whole_documents_only);
    RUN(writer_builds_a_flat_object);
    RUN(writer_nests_objects_and_arrays_with_correct_commas);
    RUN(what_the_writer_produces_is_readable_by_the_scanner);
    RUN(writer_escapes_control_characters);
    RUN(writer_writes_the_int32_extremes);
    RUN(writer_inserts_raw_fragments_verbatim);
    RUN(a_document_that_does_not_fit_is_reported_not_silently_truncated);
    RUN(an_unbalanced_document_is_reported);
    RUN(a_zero_sized_buffer_fails_rather_than_writing);
    RUN(writer_survives_a_null_buffer);
    RUN(a_discovery_sized_document_fits_and_reads_back);
    RUN(a_home_assistant_light_command_parses_field_by_field);
)
