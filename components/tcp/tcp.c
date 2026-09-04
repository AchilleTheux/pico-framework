#include <string.h>

#include "pico/stdlib.h"

#include "tcp.h"

#if TCP_SUPPORTED
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
#endif

const char *tcp_client_state_name(tcp_state_t state)
{
    switch (state) {
        case TCP_STATE_IDLE:       return "idle";
        case TCP_STATE_RESOLVING:  return "resolving";
        case TCP_STATE_CONNECTING: return "connecting";
        case TCP_STATE_CONNECTED:  return "connected";
        case TCP_STATE_WAITING:    return "waiting to retry";
        case TCP_STATE_GAVE_UP:    return "gave up";
        default:                   return "unknown";
    }
}

const char *tcp_client_result_name(tcp_result_t result)
{
    switch (result) {
        case TCP_OK:                return "ok";
        case TCP_ERR_INVALID_ARG:   return "invalid argument";
        case TCP_ERR_UNSUPPORTED:   return "no network stack on this board";
        case TCP_ERR_NOT_CONNECTED: return "not connected";
        case TCP_ERR_NO_MEMORY:     return "no pcb left to give";
        case TCP_ERR_FAILED:        return "refused";
        default:                    return "unknown";
    }
}

#if !TCP_SUPPORTED

/*
 * No network stack on this board -- see tcp.h. Every call reports
 * TCP_ERR_UNSUPPORTED rather than failing to link.
 */

tcp_result_t tcp_client_init(tcp_client_t *client)
{
    if (client != NULL) {
        memset(client, 0, sizeof(*client));
    }
    return TCP_ERR_UNSUPPORTED;
}

void tcp_client_deinit(tcp_client_t *client) { (void)client; }

tcp_result_t tcp_client_open(tcp_client_t *client, const tcp_client_config_t *config)
{
    (void)client; (void)config;
    return TCP_ERR_UNSUPPORTED;
}

tcp_result_t tcp_client_close(tcp_client_t *client)
{
    (void)client;
    return TCP_ERR_UNSUPPORTED;
}

void tcp_client_poll(tcp_client_t *client) { (void)client; }

size_t tcp_client_write(tcp_client_t *client, const void *data, size_t length)
{
    (void)client; (void)data; (void)length;
    return 0;
}

int tcp_client_read(tcp_client_t *client)
{
    (void)client;
    return -1;
}

size_t tcp_client_read_bytes(tcp_client_t *client, void *data, size_t length)
{
    (void)client; (void)data; (void)length;
    return 0;
}

#else /* TCP_SUPPORTED */

static uint32_t now_ms(void)
{
    return (uint32_t)(time_us_64() / 1000u);
}

static inline struct tcp_pcb *pcb_of(tcp_client_t *client)
{
    return (struct tcp_pcb *)client->pcb;
}

/*
 * Detach this instance from its PCB and let go of it.
 *
 * `graceful` sends a FIN; an abort sends RST and is what a failed attempt or a
 * timeout wants, since there is nothing to shut down politely. Either way the
 * PCB belongs to lwIP afterwards and must not be touched again -- clearing the
 * callbacks first means a late one cannot arrive pointing at this instance.
 *
 * Returns true when the PCB was aborted rather than closed. A caller running
 * inside an lwIP callback must turn that into ERR_ABRT: tcp_abort() frees the
 * PCB immediately, and lwIP keeps using it unless the callback says so. A
 * caller outside one can ignore it.
 */
static bool release_pcb(tcp_client_t *client, bool graceful)
{
    struct tcp_pcb *pcb = pcb_of(client);
    if (pcb == NULL) {
        return false;
    }
    client->pcb = NULL;

    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_err(pcb, NULL);

    if (graceful) {
        /* tcp_close() can fail for want of memory to send the FIN. lwIP's own
           guidance is to abort rather than leak the PCB waiting for memory
           that may not come. */
        if (tcp_close(pcb) == ERR_OK) {
            return false;
        }
    }
    tcp_abort(pcb);
    return true;
}

static void begin_attempt(tcp_client_t *client);

/* A failed attempt or a lost connection: back off and try again, unless the
   budget has run out or the caller does not want reconnection. */
static void handle_failure(tcp_client_t *client)
{
    tcp_stream_set_connected(&client->stream, false);

    if (!client->config.auto_reconnect && client->sessions > 0) {
        /* The caller asked for one connection and has had it. */
        client->state = TCP_STATE_IDLE;
        return;
    }
    if (wifi_retry_exhausted(&client->retry)) {
        client->state = TCP_STATE_GAVE_UP;
        return;
    }
    wifi_retry_fail(&client->retry, now_ms());
    client->state = TCP_STATE_WAITING;
}

/* ---------------------------------------------------------------------------
 * lwIP callbacks
 * -------------------------------------------------------------------------*/

/*
 * Hand a staged segment to lwIP. Returns what it took, which is all or nothing
 * -- tcp_write() either queues the whole buffer or fails with ERR_MEM having
 * queued none of it, so there is no partial acceptance to unpick here. The
 * caller has already limited the offer to tcp_sndbuf().
 */
static uint16_t stream_send(void *ctx, const uint8_t *data, uint16_t length)
{
    tcp_client_t *client = (tcp_client_t *)ctx;
    struct tcp_pcb *pcb = pcb_of(client);
    if (pcb == NULL || length == 0) {
        return 0;
    }

    const u16_t room = tcp_sndbuf(pcb);
    if (room < length) {
        length = room;
    }
    if (length == 0) {
        return 0;
    }

    /* Copied into lwIP's own pbufs: the bytes being offered live in a stack
       buffer inside tcp_stream_flush() and are gone the moment it returns. */
    const err_t err = tcp_write(pcb, data, length, TCP_WRITE_FLAG_COPY);
    if (err != ERR_OK) {
        return 0;
    }
    return length;
}

/* Push whatever is buffered at lwIP, as far as its send buffer allows. */
static void pump_output(tcp_client_t *client)
{
    if (client->state != TCP_STATE_CONNECTED || client->pcb == NULL) {
        return;
    }
    if (!tcp_stream_has_output(&client->stream)) {
        return;
    }

    const uint32_t before = client->stream.bytes_sent;
    tcp_stream_flush(&client->stream, tcp_sndbuf(pcb_of(client)));

    if (client->stream.bytes_sent != before) {
        /* One output per flush rather than per segment: tcp_output() is what
           actually puts a packet on the wire, and calling it once after
           queueing everything lets lwIP coalesce. */
        tcp_output(pcb_of(client));
    }
}

static err_t on_recv(void *arg, struct tcp_pcb *pcb, struct pbuf *p, err_t err)
{
    tcp_client_t *client = (tcp_client_t *)arg;

    if (client == NULL) {
        /* A callback that outlived its instance. Nothing owns the data. */
        if (p != NULL) {
            pbuf_free(p);
        }
        return ERR_OK;
    }

    if (p == NULL) {
        /* The peer closed. */
        const bool aborted = release_pcb(client, true);
        client->state = TCP_STATE_IDLE;
        if (client->config.on_closed != NULL) {
            client->config.on_closed(client->config.on_closed_arg);
        }
        handle_failure(client);

        /* Running inside lwIP's own callback: if release_pcb() had to abort,
           lwIP must be told the PCB is gone or it carries on using it. */
        return aborted ? ERR_ABRT : ERR_OK;
    }

    if (err != ERR_OK) {
        pbuf_free(p);
        return err;
    }

    /*
     * Decline rather than truncate when the buffer is full. Returning ERR_MEM
     * without freeing the pbuf is lwIP's back-pressure contract: it holds the
     * segment in pcb->refused_data and offers it again later, so the receive
     * window closes and the peer stops sending instead of the stream losing
     * bytes out of its middle. Not freeing p here is deliberate -- lwIP still
     * owns it.
     */
    if (!tcp_stream_can_accept(&client->stream, p->tot_len)) {
        return ERR_MEM;
    }

    /* Copied out of the pbuf chain, which may be several segments. */
    const u16_t received = p->tot_len;
    for (struct pbuf *q = p; q != NULL; q = q->next) {
        tcp_stream_on_received(&client->stream, q->payload, q->len);
    }

    tcp_recved(pcb, received);
    pbuf_free(p);
    return ERR_OK;
}

static err_t on_sent(void *arg, struct tcp_pcb *pcb, u16_t len)
{
    (void)pcb;
    (void)len;
    tcp_client_t *client = (tcp_client_t *)arg;
    if (client != NULL) {
        /* Space has just come free; anything still queued goes now rather than
           waiting for the next poll. */
        pump_output(client);
    }
    return ERR_OK;
}

/*
 * A connection was lost or an attempt failed. lwIP has already freed the PCB
 * by the time this runs -- touching it here, including to close it, is a
 * use-after-free -- so the pointer is dropped, not released.
 */
static void on_error(void *arg, err_t err)
{
    (void)err;
    tcp_client_t *client = (tcp_client_t *)arg;
    if (client == NULL) {
        return;
    }

    const bool was_connected = (client->state == TCP_STATE_CONNECTED);
    client->pcb = NULL;

    if (was_connected && client->config.on_closed != NULL) {
        client->config.on_closed(client->config.on_closed_arg);
    }
    handle_failure(client);
}

static err_t on_connected(void *arg, struct tcp_pcb *pcb, err_t err)
{
    tcp_client_t *client = (tcp_client_t *)arg;
    if (client == NULL) {
        return ERR_ABRT;
    }

    if (err != ERR_OK) {
        /* lwIP frees the PCB after a failed connect the same way it does on an
           error callback. */
        client->pcb = NULL;
        handle_failure(client);
        return ERR_OK;
    }

    wifi_retry_reset(&client->retry);
    client->state = TCP_STATE_CONNECTED;
    client->sessions++;
    tcp_stream_set_connected(&client->stream, true);

    tcp_sent(pcb, on_sent);

    /*
     * State first, callback second: on_connect exists to say whatever the
     * protocol expects first, and tcp_client_write() refuses unless this
     * instance already believes it is connected.
     */
    if (client->config.on_connect != NULL) {
        client->config.on_connect(client->config.on_connect_arg);
    }
    pump_output(client);
    return ERR_OK;
}

/* ---------------------------------------------------------------------------
 * Connecting
 * -------------------------------------------------------------------------*/

/* Open a connection to whatever client->host_ipv4 currently holds. */
static tcp_result_t start_connect(tcp_client_t *client)
{
    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        return TCP_ERR_NO_MEMORY;
    }
    client->pcb = pcb;

    tcp_arg(pcb, client);
    tcp_recv(pcb, on_recv);
    tcp_err(pcb, on_error);

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, client->host_ipv4);

    client->attempt_started_ms = now_ms();
    client->state = TCP_STATE_CONNECTING;

    const err_t err = tcp_connect(pcb, &addr, client->config.port, on_connected);
    if (err != ERR_OK) {
        /* tcp_connect() only frees the PCB by way of the error callback, which
           it has not called here, so this one is still ours to abort. */
        release_pcb(client, false);
        return TCP_ERR_FAILED;
    }
    return TCP_OK;
}

static void on_dns(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    tcp_client_t *client = (tcp_client_t *)arg;

    if (ipaddr == NULL) {
        handle_failure(client);
        return;
    }
    client->host_ipv4 = ip4_addr_get_u32(ip_2_ip4(ipaddr));
    if (start_connect(client) != TCP_OK) {
        handle_failure(client);
    }
}

/*
 * Resolve the host, then connect -- either at once, if the name was a dotted
 * quad or already cached, or from on_dns() once lwIP's lookup completes.
 */
static void begin_attempt(tcp_client_t *client)
{
    ip_addr_t resolved;
    const err_t err = dns_gethostbyname(client->config.host, &resolved, on_dns, client);

    if (err == ERR_OK) {
        client->host_ipv4 = ip4_addr_get_u32(ip_2_ip4(&resolved));
        if (start_connect(client) != TCP_OK) {
            handle_failure(client);
        }
        return;
    }
    if (err == ERR_INPROGRESS) {
        client->attempt_started_ms = now_ms();
        client->state = TCP_STATE_RESOLVING;
        return;
    }
    handle_failure(client);
}

/* ---------------------------------------------------------------------------
 * Public API
 * -------------------------------------------------------------------------*/

tcp_result_t tcp_client_init(tcp_client_t *client)
{
    if (client == NULL) {
        return TCP_ERR_INVALID_ARG;
    }
    memset(client, 0, sizeof(*client));
    client->state = TCP_STATE_IDLE;
    client->initialised = true;
    return TCP_OK;
}

void tcp_client_deinit(tcp_client_t *client)
{
    if (client == NULL || !client->initialised) {
        return;
    }
    release_pcb(client, true);
    tcp_stream_set_connected(&client->stream, false);
    client->initialised = false;
    client->state = TCP_STATE_IDLE;
}

tcp_result_t tcp_client_open(tcp_client_t *client, const tcp_client_config_t *config)
{
    if (client == NULL || !client->initialised || config == NULL) {
        return TCP_ERR_INVALID_ARG;
    }
    if (config->host == NULL || config->host[0] == '\0' || config->port == 0) {
        return TCP_ERR_INVALID_ARG;
    }

    /* A second open() on a live instance replaces the connection rather than
       leaking its PCB, and does so before the stream underneath it is reset. */
    release_pcb(client, true);

    if (!tcp_stream_init(&client->stream,
                         config->rx_buffer, config->rx_buffer_size,
                         config->tx_buffer, config->tx_buffer_size,
                         stream_send, client)) {
        return TCP_ERR_INVALID_ARG;
    }

    client->config = *config;
    if (client->config.connect_timeout_ms == 0) {
        client->config.connect_timeout_ms = TCP_DEFAULT_CONNECT_TIMEOUT_MS;
    }

    wifi_retry_init(&client->retry, &client->config.retry);
    client->sessions = 0;

    begin_attempt(client);
    return TCP_OK;
}

tcp_result_t tcp_client_close(tcp_client_t *client)
{
    if (client == NULL || !client->initialised) {
        return TCP_ERR_INVALID_ARG;
    }
    release_pcb(client, true);
    tcp_stream_set_connected(&client->stream, false);
    wifi_retry_reset(&client->retry);
    client->state = TCP_STATE_IDLE;
    return TCP_OK;
}

void tcp_client_poll(tcp_client_t *client)
{
    if (client == NULL || !client->initialised) {
        return;
    }

    switch (client->state) {
        case TCP_STATE_RESOLVING:
        case TCP_STATE_CONNECTING: {
            /*
             * lwIP's own connect timeout is minutes long, which is not a
             * timeframe anyone debugging a wrong port wants to sit through.
             */
            const uint32_t elapsed = now_ms() - client->attempt_started_ms;
            if (elapsed >= client->config.connect_timeout_ms) {
                release_pcb(client, false);
                handle_failure(client);
            }
            break;
        }

        case TCP_STATE_WAITING:
            if (wifi_retry_due(&client->retry, now_ms())) {
                begin_attempt(client);
            }
            break;

        case TCP_STATE_CONNECTED:
            pump_output(client);
            break;

        default:
            break;
    }
}

size_t tcp_client_write(tcp_client_t *client, const void *data, size_t length)
{
    if (client == NULL || !client->initialised) {
        return 0;
    }

    const size_t accepted = tcp_stream_write(&client->stream, data, length);
    if (accepted > 0) {
        pump_output(client);
    }
    return accepted;
}

int tcp_client_read(tcp_client_t *client)
{
    if (client == NULL || !client->initialised) {
        return -1;
    }
    return tcp_stream_read(&client->stream);
}

size_t tcp_client_read_bytes(tcp_client_t *client, void *data, size_t length)
{
    if (client == NULL || !client->initialised) {
        return 0;
    }
    return tcp_stream_read_bytes(&client->stream, data, length);
}

#endif /* TCP_SUPPORTED */
