/*
 * tcp - a poll-driven TCP client over lwIP's raw API.
 *
 * What DESIGN_DOC.md section 18 lists as a future component, sitting where
 * section 17's WiFi note says higher-level networking belongs: separate from
 * connection management. wifi.c's job ends at "there is a working link and
 * here is its address"; this component opens a connection on top of whatever
 * link is already up, and never touches the radio or the netif.
 *
 * Non-blocking, like everything else here. tcp_client_open() starts a DNS
 * lookup and a connection and returns at once; tcp_client_poll() drives the
 * reconnect state machine and pushes buffered output, the same contract as
 * wifi_poll() and mqtt_poll(). It does not call wifi_poll() or
 * cyw43_arch_poll() itself -- lwIP's core processing, which is what actually
 * resolves the lookup and moves the connection along, happens as a side
 * effect of whatever already polls the link.
 *
 * A caller with no link yet is not a caller with nothing to do:
 * tcp_client_open() may be called before wifi_is_connected() is true, and the
 * attempt simply keeps failing and retrying on this component's own backoff
 * until the link comes up.
 *
 * WHAT THIS IS NOT
 *
 * A client, not a server: it dials out, it does not listen. Accepting inbound
 * connections is a different job with different resource ownership -- a listen
 * PCB, a policy for how many peers at once, and what to do with the second one
 * -- and belongs in its own component when an application needs it, per
 * section 18's rule that components appear when there is a real reuse case.
 *
 * One connection per instance. Two connections means two tcp_client_t, each
 * with its own buffers, which is also how lwIP counts them: MEMP_NUM_TCP_PCB
 * in lwipopts.h is the ceiling.
 *
 * BOARDS WITHOUT A RADIO
 *
 * This component depends on wifi for the only network stack the framework has
 * (lwIP over the CYW43), the same way ax12 depends on half_duplex_uart for a
 * transport it does not implement itself (section 7). It compiles for every
 * board; TCP_SUPPORTED tracks WIFI_SUPPORTED, and without a radio every call
 * returns TCP_ERR_UNSUPPORTED. Check the macro rather than discovering it at
 * runtime.
 *
 * NAMING
 *
 * tcp.c includes lwIP's own lwip/tcp.h, whose raw API already owns very nearly
 * every verb this component would want: tcp_new, tcp_connect, tcp_write,
 * tcp_close, tcp_abort, tcp_bind, tcp_listen, tcp_accept, tcp_recv, tcp_sent,
 * tcp_err, tcp_output, tcp_recved -- and tcp_poll, which is a different thing
 * again from a poll loop. So the API here is prefixed tcp_client_ throughout,
 * uniformly rather than only where a collision bites, and the connect verb is
 * "open" so that reading tcp_client_open() next to lwIP's tcp_connect() in the
 * same file cannot mislead. mqtt.h made the same trade for the same reason.
 */

#ifndef PICO_FRAMEWORK_TCP_H
#define PICO_FRAMEWORK_TCP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tcp_stream.h"
#include "wifi.h"
#include "wifi_policy.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 1 when this build has a network stack to talk to. */
#define TCP_SUPPORTED WIFI_SUPPORTED

/* How long one connection attempt may take before it counts as failed. lwIP
   has its own much longer TCP timeout; this one is what makes a wrong port or
   a host that is not there fail in a human timeframe instead of minutes. */
#define TCP_DEFAULT_CONNECT_TIMEOUT_MS 10000u

typedef enum {
    TCP_OK = 0,
    TCP_ERR_INVALID_ARG,
    TCP_ERR_UNSUPPORTED,     /* no network stack on this board */
    TCP_ERR_NOT_CONNECTED,
    TCP_ERR_NO_MEMORY,       /* lwIP had no PCB left to give */
    TCP_ERR_FAILED,          /* lwIP refused the request */
} tcp_result_t;

typedef enum {
    TCP_STATE_IDLE = 0,      /* open() not called, or close()d */
    TCP_STATE_RESOLVING,     /* DNS lookup in flight */
    TCP_STATE_CONNECTING,    /* SYN sent */
    TCP_STATE_CONNECTED,
    TCP_STATE_WAITING,       /* backing off before the next attempt */
    TCP_STATE_GAVE_UP,
} tcp_state_t;

/*
 * Called once each time a connection is established -- the first one and every
 * reconnection after it.
 *
 * This is where a handshake, a login line, or a subscription goes, for the
 * same reason mqtt's on_connect exists: a reconnect leaves the peer knowing
 * nothing about the previous session, and anything the protocol expects to be
 * said first has to be said again. tcp_client_write() is safe to call from
 * inside it; the connection is up by the time it runs.
 *
 * Runs from lwIP's connected callback, so do the small thing here and leave
 * the long one to the main loop.
 */
typedef void (*tcp_client_connect_cb_t)(void *arg);

/*
 * Called when an established connection ends, whichever side ended it and
 * whether it was clean or an error. Not called for a failed connection
 * attempt -- that is a retry, not a closure, and shows up in tcp_client_state()
 * as TCP_STATE_WAITING.
 */
typedef void (*tcp_client_closed_cb_t)(void *arg);

typedef struct {
    /*
     * Borrowed, not copied, so it must outlive the connection -- the same
     * trade wifi_config_t and mqtt_config_t make.
     */
    const char *host;          /* hostname or dotted-quad */
    uint16_t port;

    /*
     * Caller-owned, and must outlive the connection. Sizing is the caller's
     * decision because only it knows the traffic: a console needs a few
     * hundred bytes, a telemetry feed rather more. Usable capacity is one byte
     * less than given (ring_buffer_capacity()).
     *
     * The receive buffer's size is also this connection's flow-control window
     * in practice: lwIP re-delivers what does not fit, so a small buffer costs
     * throughput rather than data. See tcp_stream.h.
     */
    uint8_t *rx_buffer;
    size_t rx_buffer_size;
    uint8_t *tx_buffer;
    size_t tx_buffer_size;

    /* 0 => TCP_DEFAULT_CONNECT_TIMEOUT_MS. */
    uint32_t connect_timeout_ms;

    /* Optional. */
    tcp_client_connect_cb_t on_connect;
    void *on_connect_arg;
    tcp_client_closed_cb_t on_closed;
    void *on_closed_arg;

    /*
     * Reconnect after the peer closes or the connection errors. A robot that
     * loses its control link usually wants it back; a client doing one request
     * and stopping does not.
     */
    bool auto_reconnect;

    wifi_retry_config_t retry;
} tcp_client_config_t;

typedef struct {
    tcp_client_config_t config;
    tcp_state_t state;
    wifi_retry_t retry;
    tcp_stream_t stream;

    /*
     * lwIP's struct tcp_pcb*, kept as void* so this header needs no lwIP
     * include and compiles unchanged on a board with no network stack at all.
     * tcp.c is the only file that casts it back.
     */
    void *pcb;

    uint32_t host_ipv4;
    uint32_t attempt_started_ms;
    uint32_t sessions;

    bool initialised;
} tcp_client_t;

/* Zero the instance. Allocates nothing yet -- lwIP's PCB is created by the
   first tcp_client_open(). */
tcp_result_t tcp_client_init(tcp_client_t *client);

void tcp_client_deinit(tcp_client_t *client);

/*
 * Begin connecting: resolve `config->host` and open a connection to it.
 * Returns as soon as the attempt has started; watch tcp_client_state() or
 * tcp_client_is_connected() for the outcome, the same contract as
 * wifi_connect() and mqtt_connect().
 *
 * A DNS or connect failure does not fail this call -- it starts the retry loop
 * instead, the same way a lost connection later does.
 */
tcp_result_t tcp_client_open(tcp_client_t *client, const tcp_client_config_t *config);

/* Close the connection if one is open, and stop trying. Buffered output is
   discarded rather than flushed: a caller that needs it delivered waits for
   tcp_client_pending() to reach zero before closing. */
tcp_result_t tcp_client_close(tcp_client_t *client);

/*
 * Drive the retry timer, the connect timeout, and the outgoing buffer. Call it
 * every time round the main loop, alongside wifi_poll(). This does not poll
 * the radio or lwIP itself, so it is not a substitute for wifi_poll() and has
 * nothing to do without something else already polling the link.
 */
void tcp_client_poll(tcp_client_t *client);

/* ---------------------------------------------------------------------------
 * Sending and receiving
 * -------------------------------------------------------------------------*/

/*
 * Queue bytes for the peer. Returns how many were accepted into the outgoing
 * buffer, which is fewer than asked when that buffer is full and zero when
 * there is no connection.
 *
 * Buffered, not sent, at the moment of the call: what actually goes on the
 * wire happens here if lwIP has room and in tcp_client_poll() otherwise. A
 * caller that needs to know it left waits for tcp_client_pending() to reach
 * zero.
 */
size_t tcp_client_write(tcp_client_t *client, const void *data, size_t length);

/* The next received byte, or -1 when there is none. Shaped for cli_stream_t,
   so a console can be driven over a socket the same way it is over a UART. */
int tcp_client_read(tcp_client_t *client);

size_t tcp_client_read_bytes(tcp_client_t *client, void *data, size_t length);

/* ---------------------------------------------------------------------------
 * Status
 * -------------------------------------------------------------------------*/

static inline tcp_state_t tcp_client_state(const tcp_client_t *client)
{
    return client->state;
}

static inline bool tcp_client_is_connected(const tcp_client_t *client)
{
    return client->state == TCP_STATE_CONNECTED;
}

/* Bytes received and waiting to be read. */
static inline size_t tcp_client_available(const tcp_client_t *client)
{
    return tcp_stream_available(&client->stream);
}

/* Bytes written but not yet handed to lwIP. Zero means everything has been
   given to the stack -- not that the peer has acknowledged it. */
static inline size_t tcp_client_pending(const tcp_client_t *client)
{
    return tcp_stream_pending(&client->stream);
}

/*
 * How many connections have been established since the last tcp_client_open().
 * One after the first, each reconnect adds another; open() starts the count
 * again. A caller that wants to notice a reconnection without a callback can
 * watch this.
 */
static inline uint32_t tcp_client_sessions(const tcp_client_t *client)
{
    return client->sessions;
}

/* How many attempts the current outage has taken. Zero when connected. */
static inline uint32_t tcp_client_attempts(const tcp_client_t *client)
{
    return wifi_retry_attempts(&client->retry);
}

static inline uint32_t tcp_client_bytes_sent(const tcp_client_t *client)
{
    return client->stream.bytes_sent;
}

static inline uint32_t tcp_client_bytes_received(const tcp_client_t *client)
{
    return client->stream.bytes_received;
}

/* Output lost to a full outgoing buffer. A growing count means the buffer is
   too small for what this application writes between polls. */
static inline uint32_t tcp_client_dropped(const tcp_client_t *client)
{
    return client->stream.dropped_outgoing;
}

const char *tcp_client_state_name(tcp_state_t state);
const char *tcp_client_result_name(tcp_result_t result);

#ifdef __cplusplus
}
#endif

#endif /* PICO_FRAMEWORK_TCP_H */
