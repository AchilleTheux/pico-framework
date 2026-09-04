/*
 * udp - poll-driven UDP datagrams over lwIP's raw API.
 *
 * What DESIGN_DOC.md section 18 lists as a future component, in the place
 * section 17 reserves for higher-level networking: above wifi, which owns the
 * link, and knowing nothing about the radio.
 *
 * UDP is the framework's answer to the traffic TCP is wrong for -- telemetry
 * that is worth having now or not at all, and discovery, where a device has to
 * announce itself to a network it has never been told anything about. There is
 * no connection, no retry and no ordering here, deliberately: a datagram is
 * sent once and either arrives or does not, and a caller that needs more than
 * that wants tcp instead.
 *
 * Sending is immediate rather than buffered -- there is no send window to wait
 * for, so udp_socket_send_to() either hands the datagram to the stack or tells
 * the caller why it could not. Receiving is by callback, as in mqtt, because a
 * datagram is a message rather than a stream and a byte FIFO would lose the
 * boundaries that make it one.
 *
 * There is no udp_socket_poll(): this component has no state machine to drive.
 * Datagrams arrive as a side effect of whatever already polls the link, which
 * is wifi_poll(), and the callback runs from inside it.
 *
 * BROADCAST
 *
 * udp_socket_broadcast() sends to 255.255.255.255, which reaches every host on
 * the local network without knowing the subnet -- the useful property when a
 * robot is looking for a control station on somebody else's field network.
 * Multicast is not supported: joining a group needs IGMP, which this build's
 * lwipopts.h does not enable, and enabling it would cost flash and RAM in
 * every wifi image for something nothing here has asked for yet.
 *
 * BOARDS WITHOUT A RADIO
 *
 * As with wifi, mqtt and tcp: the component compiles everywhere, UDP_SUPPORTED
 * tracks WIFI_SUPPORTED, and without a radio every call returns
 * UDP_ERR_UNSUPPORTED. Check the macro rather than discovering it at runtime.
 *
 * NAMING
 *
 * udp.c includes lwIP's own lwip/udp.h, which already owns udp_new, udp_bind,
 * udp_connect, udp_disconnect, udp_send, udp_sendto, udp_recv and udp_remove
 * -- every verb this component wants. So the API here is prefixed
 * udp_socket_ throughout, uniformly rather than only where a collision bites.
 * mqtt.h and tcp.h made the same trade for the same reason. "Socket" here
 * means a bound local endpoint, not a BSD socket: LWIP_SOCKET is 0 in this
 * build and there is no file descriptor anywhere near this.
 */

#ifndef PICO_FRAMEWORK_UDP_H
#define PICO_FRAMEWORK_UDP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "udp_policy.h"
#include "wifi.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this build has a network stack to talk to. */
#define UDP_SUPPORTED WIFI_SUPPORTED

/*
 * Largest datagram this component will deliver in one piece. Longer ones are
 * counted in udp_socket_datagrams_dropped() rather than handed over truncated,
 * on the same view mqtt takes: a fragment given to a caller expecting the
 * whole message is worse than no callback at all.
 *
 * Sized for telemetry and discovery rather than for bulk. Raise it to
 * UDP_MAX_PAYLOAD if a protocol needs the whole datagram; it costs that many
 * bytes of RAM per socket.
 */
#ifndef UDP_MAX_DATAGRAM
#define UDP_MAX_DATAGRAM 512u
#endif

typedef enum {
    UDP_OK = 0,
    UDP_ERR_INVALID_ARG,
    UDP_ERR_UNSUPPORTED,     /* no network stack on this board */
    UDP_ERR_NOT_OPEN,
    UDP_ERR_NO_MEMORY,       /* lwIP had no pbuf or PCB left to give */
    UDP_ERR_TOO_LONG,        /* past UDP_MAX_PAYLOAD; see udp_policy.h */
    UDP_ERR_RESOLVING,       /* hostname lookup started; try again shortly */
    UDP_ERR_UNSUPPORTED_ADDRESS, /* multicast; see the header comment */
    UDP_ERR_FAILED,          /* lwIP refused to send it */
} udp_result_t;

/*
 * A datagram arrived. `from` is the sender, `data` points at `length` bytes
 * valid only for the duration of the call -- copy anything worth keeping.
 *
 * Runs from lwIP's receive path inside whatever is polling the link, so the
 * same rule as mqtt's on_message applies: do the small thing here and leave
 * the long one to the main loop. udp_socket_send_to_endpoint() is safe to call
 * from inside it, which is what makes a request/response protocol a few lines.
 */
typedef void (*udp_socket_datagram_cb_t)(void *arg, const udp_endpoint_t *from,
                                         const uint8_t *data, size_t length);

typedef struct {
    /*
     * Port to receive on. 0 asks lwIP for an ephemeral one, which is what a
     * client that only sends and awaits replies wants; a service others must
     * find needs a fixed number. udp_socket_local_port() reports what was
     * actually bound.
     */
    uint16_t local_port;

    /*
     * Allow sending to the broadcast address. Off by default because a
     * broadcast reaches every host on the network and is worth asking for
     * deliberately.
     */
    bool broadcast;

    /* Optional: NULL is a legal way to run a send-only socket. */
    udp_socket_datagram_cb_t on_datagram;
    void *on_datagram_arg;
} udp_socket_config_t;

typedef struct {
    udp_socket_config_t config;

    /*
     * lwIP's struct udp_pcb*, kept as void* so this header needs no lwIP
     * include and compiles unchanged on a board with no network stack at all.
     * udp.c is the only file that casts it back.
     */
    void *pcb;

    /* Reassembly space for one incoming datagram. A pbuf may be a chain, and
       the callback promises one contiguous buffer. */
    uint8_t buffer[UDP_MAX_DATAGRAM];

    uint32_t datagrams_sent;
    uint32_t datagrams_received;
    uint32_t datagrams_dropped;

    bool open;
    bool initialised;
} udp_socket_t;

/* Zero the instance. Allocates nothing yet -- lwIP's PCB is created by
   udp_socket_open(). */
udp_result_t udp_socket_init(udp_socket_t *socket);

void udp_socket_deinit(udp_socket_t *socket);

/*
 * Bind the local port and start receiving. Unlike tcp_client_open() this
 * either succeeds or fails outright: binding a port is a local operation with
 * nothing to wait for and nothing to retry.
 */
udp_result_t udp_socket_open(udp_socket_t *socket, const udp_socket_config_t *config);

udp_result_t udp_socket_close(udp_socket_t *socket);

/* ---------------------------------------------------------------------------
 * Sending
 * -------------------------------------------------------------------------*/

/*
 * Send one datagram to `host`, which may be a dotted quad or a hostname.
 *
 * A dotted quad is parsed here, strictly -- see udp_policy.h for why not
 * lwIP's own parser. A hostname needs DNS, and DNS is not instant: when the
 * name is already in lwIP's cache the datagram goes at once, and when it is
 * not this returns UDP_ERR_RESOLVING having started the lookup and sent
 * nothing. Call again in a moment and the answer will be cached.
 *
 * That is a deliberately blunt contract, and it suits UDP: a datagram is
 * already something that may not arrive, so a caller is written to repeat
 * itself anyway. A caller that cannot tolerate it resolves once at startup and
 * then uses udp_socket_send_to_endpoint().
 */
udp_result_t udp_socket_send_to(udp_socket_t *socket, const char *host, uint16_t port,
                                const void *data, size_t length);

/* Send to an already-resolved address -- the form to use when replying to a
   received datagram, since the sender arrived with the message. No DNS, so no
   UDP_ERR_RESOLVING. */
udp_result_t udp_socket_send_to_endpoint(udp_socket_t *socket, const udp_endpoint_t *to,
                                         const void *data, size_t length);

/*
 * Send to 255.255.255.255, reaching every host on the local network. Requires
 * `broadcast` in the configuration; without it this returns
 * UDP_ERR_INVALID_ARG rather than quietly sending to everyone.
 */
udp_result_t udp_socket_broadcast(udp_socket_t *socket, uint16_t port,
                                  const void *data, size_t length);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

static inline bool udp_socket_is_open(const udp_socket_t *socket)
{
    return socket->open;
}

/* The port actually bound, which is the one lwIP chose when the configuration
   asked for 0. Zero when the socket is not open. */
uint16_t udp_socket_local_port(const udp_socket_t *socket);

static inline uint32_t udp_socket_datagrams_sent(const udp_socket_t *socket)
{
    return socket->datagrams_sent;
}

static inline uint32_t udp_socket_datagrams_received(const udp_socket_t *socket)
{
    return socket->datagrams_received;
}

/* Datagrams that arrived longer than UDP_MAX_DATAGRAM and were counted rather
   than truncated. */
static inline uint32_t udp_socket_datagrams_dropped(const udp_socket_t *socket)
{
    return socket->datagrams_dropped;
}

const char *udp_result_name(udp_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_UDP_H */
