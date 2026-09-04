/*
 * Host-side tests for UDP address and endpoint validation.
 *
 * The parser is the reason this file exists. lwIP's ipaddr_aton() is
 * inet_aton()'s, which reads "192.168.1.010" as .8, "10.1" as 10.0.0.1 and
 * "0x0a" as ten -- so a typo in a configured peer address does not fail, it
 * silently addresses a different host. Every one of those forms is pinned here
 * as a rejection.
 */

#include <string.h>

#include "test.h"

#include "udp_policy.h"

static bool parses_to(const char *text, uint8_t a, uint8_t b, uint8_t c, uint8_t d)
{
    uint8_t out[4] = { 0, 0, 0, 0 };
    if (!udp_ipv4_parse(text, out)) {
        return false;
    }
    return out[0] == a && out[1] == b && out[2] == c && out[3] == d;
}

static bool rejects(const char *text)
{
    uint8_t out[4] = { 9, 9, 9, 9 };
    const bool parsed = udp_ipv4_parse(text, out);
    if (parsed) {
        return false;
    }
    /* A rejection must leave the caller's buffer alone. */
    return out[0] == 9 && out[1] == 9 && out[2] == 9 && out[3] == 9;
}

/* ---------------------------------------------------------------------------
 * Parsing
 * -------------------------------------------------------------------------*/

TEST(ordinary_addresses_parse)
{
    CHECK(parses_to("192.168.1.31", 192, 168, 1, 31));
    CHECK(parses_to("10.0.0.1", 10, 0, 0, 1));
    CHECK(parses_to("0.0.0.0", 0, 0, 0, 0));
    CHECK(parses_to("255.255.255.255", 255, 255, 255, 255));
    CHECK(parses_to("1.2.3.4", 1, 2, 3, 4));
}

TEST(a_leading_zero_is_rejected_rather_than_guessed_at)
{
    /*
     * inet_aton() reads these as octal: "010" is eight. Nobody writing a
     * configuration means that, and a device quietly talking to .8 instead of
     * .10 is a far worse afternoon than an error message.
     */
    CHECK(rejects("192.168.1.010"));
    CHECK(rejects("010.1.1.1"));
    CHECK(rejects("1.1.1.00"));

    /* A bare zero is still a zero. */
    CHECK(parses_to("0.0.0.0", 0, 0, 0, 0));
}

TEST(short_and_long_forms_are_rejected)
{
    CHECK(rejects("10.1"));            /* inet_aton: 10.0.0.1 */
    CHECK(rejects("1.2.3"));
    CHECK(rejects("1.2.3.4.5"));
    CHECK(rejects("1"));
}

TEST(an_octet_past_255_is_rejected)
{
    CHECK(rejects("256.1.1.1"));
    CHECK(rejects("1.1.1.256"));
    CHECK(rejects("999.999.999.999"));

    /* Three digits is the most an octet can have; a fourth is not a bigger
       number, it is a malformed field. */
    CHECK(rejects("1.1.1.0000"));
}

TEST(anything_that_is_not_four_decimal_octets_is_rejected)
{
    CHECK(rejects(""));
    CHECK(rejects(NULL));
    CHECK(rejects("0x0a.1.1.1"));      /* inet_aton: hexadecimal */
    CHECK(rejects("1.2.3.4x"));
    CHECK(rejects("1.2.3.4 "));
    CHECK(rejects(" 1.2.3.4"));
    CHECK(rejects("1.2.3."));
    CHECK(rejects(".1.2.3"));
    CHECK(rejects("1..2.3"));
    CHECK(rejects("-1.2.3.4"));
    CHECK(rejects("1.2.3.-4"));
}

TEST(a_hostname_is_rejected_which_is_how_a_caller_knows_to_use_dns)
{
    CHECK(rejects("broker.hivemq.com"));
    CHECK(rejects("localhost"));
    CHECK(rejects("robot-1.local"));
}

TEST(parse_survives_a_null_output)
{
    CHECK(!udp_ipv4_parse("1.2.3.4", NULL));
}

/* ---------------------------------------------------------------------------
 * Formatting
 * -------------------------------------------------------------------------*/

TEST(formatting_round_trips)
{
    const uint8_t addr[4] = { 192, 168, 1, 31 };
    char text[UDP_ADDRESS_LENGTH];

    CHECK_EQ_INT(udp_ipv4_format(addr, text, sizeof(text)), 12);
    CHECK_EQ_STR(text, "192.168.1.31");

    uint8_t back[4];
    CHECK(udp_ipv4_parse(text, back));
    CHECK(memcmp(addr, back, sizeof(addr)) == 0);
}

TEST(the_longest_address_fits_the_advertised_buffer)
{
    const uint8_t addr[4] = { 255, 255, 255, 255 };
    char text[UDP_ADDRESS_LENGTH];

    CHECK_EQ_INT(udp_ipv4_format(addr, text, sizeof(text)), 15);
    CHECK_EQ_STR(text, "255.255.255.255");
    CHECK_EQ_INT(UDP_ADDRESS_LENGTH, 16);
}

TEST(a_buffer_too_small_reports_zero_rather_than_a_half_address)
{
    const uint8_t addr[4] = { 192, 168, 1, 31 };
    char text[8] = { 'x' };

    CHECK_EQ_INT(udp_ipv4_format(addr, text, sizeof(text)), 0);
    CHECK_EQ_STR(text, "");

    CHECK_EQ_INT(udp_ipv4_format(addr, text, 0), 0);
    CHECK_EQ_INT(udp_ipv4_format(addr, NULL, sizeof(text)), 0);
    CHECK_EQ_INT(udp_ipv4_format(NULL, text, sizeof(text)), 0);
}

/* ---------------------------------------------------------------------------
 * Classification
 * -------------------------------------------------------------------------*/

TEST(only_the_limited_broadcast_address_is_broadcast)
{
    const uint8_t everyone[4] = { 255, 255, 255, 255 };
    const uint8_t subnet[4] = { 192, 168, 1, 255 };
    const uint8_t host[4] = { 192, 168, 1, 31 };

    CHECK(udp_ipv4_is_broadcast(everyone));

    /* A subnet broadcast is one too, but not one this component can recognise
       without knowing the mask -- and it does not know the mask. */
    CHECK(!udp_ipv4_is_broadcast(subnet));
    CHECK(!udp_ipv4_is_broadcast(host));
    CHECK(!udp_ipv4_is_broadcast(NULL));
}

TEST(multicast_is_224_over_4)
{
    const uint8_t low[4] = { 224, 0, 0, 1 };
    const uint8_t high[4] = { 239, 255, 255, 255 };
    const uint8_t below[4] = { 223, 255, 255, 255 };
    const uint8_t above[4] = { 240, 0, 0, 1 };

    CHECK(udp_ipv4_is_multicast(low));
    CHECK(udp_ipv4_is_multicast(high));
    CHECK(!udp_ipv4_is_multicast(below));
    CHECK(!udp_ipv4_is_multicast(above));
    CHECK(!udp_ipv4_is_multicast(NULL));
}

/* ---------------------------------------------------------------------------
 * Endpoints and payloads
 * -------------------------------------------------------------------------*/

TEST(an_endpoint_needs_a_host_and_a_usable_port)
{
    CHECK_EQ_INT(udp_check_endpoint("192.168.1.31", 5000), UDP_ENDPOINT_OK);
    CHECK_EQ_INT(udp_check_endpoint("robot.local", 1), UDP_ENDPOINT_OK);
    CHECK_EQ_INT(udp_check_endpoint("host", 65535), UDP_ENDPOINT_OK);

    CHECK_EQ_INT(udp_check_endpoint(NULL, 5000), UDP_ENDPOINT_NO_HOST);
    CHECK_EQ_INT(udp_check_endpoint("", 5000), UDP_ENDPOINT_NO_HOST);

    /* Port 0 is reserved; a datagram addressed to it goes nowhere. */
    CHECK_EQ_INT(udp_check_endpoint("192.168.1.31", 0), UDP_ENDPOINT_NO_PORT);
}

TEST(a_payload_is_bounded_by_what_one_datagram_carries)
{
    CHECK(udp_payload_fits(0));
    CHECK(udp_payload_fits(1));
    CHECK(udp_payload_fits(UDP_MAX_PAYLOAD));
    CHECK(!udp_payload_fits(UDP_MAX_PAYLOAD + 1));

    /* 1500 Ethernet MTU less 20 of IPv4 header and 8 of UDP header. */
    CHECK_EQ_INT(UDP_MAX_PAYLOAD, 1472);
}

TEST(every_result_has_a_name)
{
    CHECK_EQ_STR(udp_endpoint_result_name(UDP_ENDPOINT_OK), "ok");
    CHECK_EQ_STR(udp_endpoint_result_name(UDP_ENDPOINT_NO_HOST), "no host");
    CHECK_EQ_STR(udp_endpoint_result_name(UDP_ENDPOINT_NO_PORT),
                 "port 0 cannot be addressed");
    CHECK_EQ_STR(udp_endpoint_result_name((udp_endpoint_result_t)99), "unknown");
}

TEST_MAIN(
    RUN(ordinary_addresses_parse);
    RUN(a_leading_zero_is_rejected_rather_than_guessed_at);
    RUN(short_and_long_forms_are_rejected);
    RUN(an_octet_past_255_is_rejected);
    RUN(anything_that_is_not_four_decimal_octets_is_rejected);
    RUN(a_hostname_is_rejected_which_is_how_a_caller_knows_to_use_dns);
    RUN(parse_survives_a_null_output);
    RUN(formatting_round_trips);
    RUN(the_longest_address_fits_the_advertised_buffer);
    RUN(a_buffer_too_small_reports_zero_rather_than_a_half_address);
    RUN(only_the_limited_broadcast_address_is_broadcast);
    RUN(multicast_is_224_over_4);
    RUN(an_endpoint_needs_a_host_and_a_usable_port);
    RUN(a_payload_is_bounded_by_what_one_datagram_carries);
    RUN(every_result_has_a_name);
)
