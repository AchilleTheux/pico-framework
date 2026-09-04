/*
 * udp_policy - addresses and endpoints, as decisions rather than as sockets.
 *
 * The parts of sending a datagram that are choices: whether a string is
 * actually an address, which addresses mean "everyone on this network", and
 * how much payload will survive the trip. No lwIP and no Pico SDK dependency,
 * so all of it is unit-tested on the host.
 *
 * WHY A PARSER, WHEN lwIP HAS ipaddr_aton()
 *
 * Because lwIP's is inet_aton()'s, and inet_aton() is generous in ways nobody
 * wants from a configuration field. It accepts "10.1" as 10.0.0.1, reads a
 * leading zero as octal so that "192.168.1.010" addresses .8, and takes "0x0a"
 * as hexadecimal. A typo in a broker address or a peer address is then not a
 * rejected setting but a device quietly talking to the wrong host, which is a
 * far worse afternoon than an error message.
 *
 * This parser takes exactly four decimal octets, no leading zeros, nothing
 * before or after, and rejects everything else.
 */

#ifndef PICO_FRAMEWORK_UDP_POLICY_H
#define PICO_FRAMEWORK_UDP_POLICY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Longest dotted quad, terminator included: "255.255.255.255". */
#define UDP_ADDRESS_LENGTH 16u

/*
 * Most payload that fits one datagram on an ordinary Ethernet/WiFi network:
 * 1500 MTU less 20 bytes of IPv4 header and 8 of UDP header.
 *
 * IP would fragment anything larger and lwIP would need the pbufs to hold it,
 * so a longer datagram is not a slower datagram but a lost one. Refusing it
 * here means the caller finds out at the call rather than from a peer that
 * never answers.
 */
#define UDP_MAX_PAYLOAD 1472u

/* A remote address and port, in a form that needs no lwIP types -- so a
   receive callback can report the sender to code that has never heard of
   ip_addr_t. */
typedef struct {
    uint8_t address[4];   /* network order: address[0] is the first octet */
    uint16_t port;
} udp_endpoint_t;

typedef enum {
    UDP_ENDPOINT_OK = 0,
    UDP_ENDPOINT_NO_HOST,
    UDP_ENDPOINT_NO_PORT,     /* port 0 is reserved and cannot be addressed */
} udp_endpoint_result_t;

udp_endpoint_result_t udp_check_endpoint(const char *host, uint16_t port);
const char *udp_endpoint_result_name(udp_endpoint_result_t result);

/*
 * Strict dotted-quad parse. False -- leaving `out` untouched -- for anything
 * that is not exactly four decimal octets, which includes every hostname, so
 * a false here is the caller's cue to try DNS rather than an error.
 */
bool udp_ipv4_parse(const char *text, uint8_t out[4]);

/*
 * Write `addr` as a dotted quad. Returns the length written, or 0 when the
 * buffer is too small; UDP_ADDRESS_LENGTH always suffices.
 */
size_t udp_ipv4_format(const uint8_t addr[4], char *out, size_t size);

/* 255.255.255.255 -- the limited broadcast address, which is the one a device
   with no idea what subnet it is on can still use to be found. */
bool udp_ipv4_is_broadcast(const uint8_t addr[4]);

/* 224.0.0.0/4. Recognised so that it can be refused with a reason: joining a
   group needs IGMP, which this build's lwipopts.h does not enable. */
bool udp_ipv4_is_multicast(const uint8_t addr[4]);

/* Is a payload of this length worth handing to the stack? See
   UDP_MAX_PAYLOAD. */
bool udp_payload_fits(size_t length);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_UDP_POLICY_H */
