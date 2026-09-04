#include <string.h>

#include "pico/stdlib.h"

#include "udp.h"

#if UDP_SUPPORTED
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#endif

const char *udp_result_name(udp_result_t result)
{
    switch (result) {
        case UDP_OK:                      return "ok";
        case UDP_ERR_INVALID_ARG:         return "invalid argument";
        case UDP_ERR_UNSUPPORTED:         return "no network stack on this board";
        case UDP_ERR_NOT_OPEN:            return "socket not open";
        case UDP_ERR_NO_MEMORY:           return "no buffer left to give";
        case UDP_ERR_TOO_LONG:            return "longer than one datagram";
        case UDP_ERR_RESOLVING:           return "resolving the name, try again";
        case UDP_ERR_UNSUPPORTED_ADDRESS: return "multicast is not supported";
        case UDP_ERR_FAILED:              return "refused";
        default:                          return "unknown";
    }
}

#if !UDP_SUPPORTED

/*
 * No network stack on this board -- see udp.h. Every call reports
 * UDP_ERR_UNSUPPORTED rather than failing to link.
 */

udp_result_t udp_socket_init(udp_socket_t *socket)
{
    if (socket != NULL) {
        memset(socket, 0, sizeof(*socket));
    }
    return UDP_ERR_UNSUPPORTED;
}

void udp_socket_deinit(udp_socket_t *socket) { (void)socket; }

udp_result_t udp_socket_open(udp_socket_t *socket, const udp_socket_config_t *config)
{
    (void)socket; (void)config;
    return UDP_ERR_UNSUPPORTED;
}

udp_result_t udp_socket_close(udp_socket_t *socket)
{
    (void)socket;
    return UDP_ERR_UNSUPPORTED;
}

udp_result_t udp_socket_send_to(udp_socket_t *socket, const char *host, uint16_t port,
                                const void *data, size_t length)
{
    (void)socket; (void)host; (void)port; (void)data; (void)length;
    return UDP_ERR_UNSUPPORTED;
}

udp_result_t udp_socket_send_to_endpoint(udp_socket_t *socket, const udp_endpoint_t *to,
                                         const void *data, size_t length)
{
    (void)socket; (void)to; (void)data; (void)length;
    return UDP_ERR_UNSUPPORTED;
}

udp_result_t udp_socket_broadcast(udp_socket_t *socket, uint16_t port,
                                  const void *data, size_t length)
{
    (void)socket; (void)port; (void)data; (void)length;
    return UDP_ERR_UNSUPPORTED;
}

uint16_t udp_socket_local_port(const udp_socket_t *socket)
{
    (void)socket;
    return 0;
}

#else /* UDP_SUPPORTED */

static inline struct udp_pcb *pcb_of(const udp_socket_t *socket)
{
    return (struct udp_pcb *)socket->pcb;
}

/*
 * A datagram arrived. lwIP hands over a pbuf chain and the sender's address;
 * this flattens the one and translates the other into a udp_endpoint_t, so the
 * callback never sees an lwIP type.
 */
static void on_recv(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                    const ip_addr_t *addr, u16_t port)
{
    (void)pcb;
    udp_socket_t *socket = (udp_socket_t *)arg;

    if (p == NULL) {
        return;
    }
    if (socket == NULL) {
        pbuf_free(p);
        return;
    }

    /*
     * Counted, not truncated. Handing over the first 512 bytes of a longer
     * message and saying nothing would leave a caller parsing a fragment as if
     * it were the whole thing.
     */
    if (p->tot_len > sizeof(socket->buffer)) {
        socket->datagrams_dropped++;
        pbuf_free(p);
        return;
    }

    const u16_t copied = pbuf_copy_partial(p, socket->buffer, p->tot_len, 0);
    const u16_t total = p->tot_len;
    pbuf_free(p);

    if (copied != total) {
        socket->datagrams_dropped++;
        return;
    }

    socket->datagrams_received++;

    if (socket->config.on_datagram == NULL) {
        return;
    }

    udp_endpoint_t from;
    memset(&from, 0, sizeof(from));
    from.port = port;
    if (addr != NULL) {
        const uint32_t raw = ip4_addr_get_u32(ip_2_ip4(addr));
        /* ip4_addr_get_u32() is already network order, so the first octet is
           the low byte on this little-endian part. */
        memcpy(from.address, &raw, sizeof(from.address));
    }

    socket->config.on_datagram(socket->config.on_datagram_arg, &from,
                               socket->buffer, total);
}

/* Hand one datagram to lwIP, addressed at an already-resolved endpoint. */
static udp_result_t send_datagram(udp_socket_t *socket, const uint8_t address[4],
                                  uint16_t port, const void *data, size_t length)
{
    if (!udp_payload_fits(length)) {
        return UDP_ERR_TOO_LONG;
    }
    if (udp_ipv4_is_multicast(address)) {
        return UDP_ERR_UNSUPPORTED_ADDRESS;
    }
    if (port == 0) {
        return UDP_ERR_INVALID_ARG;
    }

    struct pbuf *p = pbuf_alloc(PBUF_TRANSPORT, (u16_t)length, PBUF_RAM);
    if (p == NULL) {
        return UDP_ERR_NO_MEMORY;
    }
    if (length > 0) {
        memcpy(p->payload, data, length);
    }

    ip_addr_t addr;
    uint32_t raw = 0;
    memcpy(&raw, address, sizeof(uint32_t));
    ip_addr_set_ip4_u32(&addr, raw);

    const err_t err = udp_sendto(pcb_of(socket), p, &addr, port);
    pbuf_free(p);

    if (err != ERR_OK) {
        return (err == ERR_MEM) ? UDP_ERR_NO_MEMORY : UDP_ERR_FAILED;
    }
    socket->datagrams_sent++;
    return UDP_OK;
}

/*
 * lwIP wants somewhere to report a completed lookup. There is nothing to do
 * with it here: the answer lands in lwIP's own DNS cache, which is where the
 * caller's next udp_socket_send_to() will find it. See that function's
 * contract in udp.h.
 */
static void on_dns(const char *name, const ip_addr_t *ipaddr, void *arg)
{
    (void)name;
    (void)ipaddr;
    (void)arg;
}

udp_result_t udp_socket_init(udp_socket_t *socket)
{
    if (socket == NULL) {
        return UDP_ERR_INVALID_ARG;
    }
    memset(socket, 0, sizeof(*socket));
    socket->initialised = true;
    return UDP_OK;
}

void udp_socket_deinit(udp_socket_t *socket)
{
    if (socket == NULL || !socket->initialised) {
        return;
    }
    udp_socket_close(socket);
    socket->initialised = false;
}

udp_result_t udp_socket_open(udp_socket_t *socket, const udp_socket_config_t *config)
{
    if (socket == NULL || !socket->initialised || config == NULL) {
        return UDP_ERR_INVALID_ARG;
    }

    /* A second open() rebinds rather than leaking the previous PCB. */
    udp_socket_close(socket);

    struct udp_pcb *pcb = udp_new();
    if (pcb == NULL) {
        return UDP_ERR_NO_MEMORY;
    }

    const err_t err = udp_bind(pcb, IP4_ADDR_ANY, config->local_port);
    if (err != ERR_OK) {
        udp_remove(pcb);
        return (err == ERR_USE) ? UDP_ERR_INVALID_ARG : UDP_ERR_FAILED;
    }

    if (config->broadcast) {
        /*
         * With IP_SOF_BROADCAST at lwIP's default of 0 the stack does not
         * check this flag, so a broadcast would go out without it. It is set
         * anyway: it records the intent where the PCB can be inspected, and it
         * keeps this working if that option is ever turned on in lwipopts.h.
         */
        ip_set_option(pcb, SOF_BROADCAST);
    }

    socket->config = *config;
    socket->pcb = pcb;
    socket->open = true;

    udp_recv(pcb, on_recv, socket);
    return UDP_OK;
}

udp_result_t udp_socket_close(udp_socket_t *socket)
{
    if (socket == NULL || !socket->initialised) {
        return UDP_ERR_INVALID_ARG;
    }
    if (socket->pcb != NULL) {
        /* Detach the callback before the PCB goes, so a datagram already in
           flight cannot arrive pointing at a closed socket. */
        udp_recv(pcb_of(socket), NULL, NULL);
        udp_remove(pcb_of(socket));
        socket->pcb = NULL;
    }
    socket->open = false;
    return UDP_OK;
}

udp_result_t udp_socket_send_to(udp_socket_t *socket, const char *host, uint16_t port,
                                const void *data, size_t length)
{
    if (socket == NULL || !socket->initialised || (data == NULL && length != 0)) {
        return UDP_ERR_INVALID_ARG;
    }
    if (udp_check_endpoint(host, port) != UDP_ENDPOINT_OK) {
        return UDP_ERR_INVALID_ARG;
    }
    if (!socket->open) {
        return UDP_ERR_NOT_OPEN;
    }

    /* A dotted quad needs no lookup, and is parsed strictly here rather than
       by lwIP -- see udp_policy.h. */
    uint8_t address[4];
    if (udp_ipv4_parse(host, address)) {
        return send_datagram(socket, address, port, data, length);
    }

    ip_addr_t resolved;
    const err_t err = dns_gethostbyname(host, &resolved, on_dns, socket);
    if (err == ERR_OK) {
        const uint32_t raw = ip4_addr_get_u32(ip_2_ip4(&resolved));
        memcpy(address, &raw, sizeof(address));
        return send_datagram(socket, address, port, data, length);
    }
    if (err == ERR_INPROGRESS) {
        return UDP_ERR_RESOLVING;
    }
    return UDP_ERR_FAILED;
}

udp_result_t udp_socket_send_to_endpoint(udp_socket_t *socket, const udp_endpoint_t *to,
                                         const void *data, size_t length)
{
    if (socket == NULL || !socket->initialised || to == NULL ||
        (data == NULL && length != 0)) {
        return UDP_ERR_INVALID_ARG;
    }
    if (!socket->open) {
        return UDP_ERR_NOT_OPEN;
    }
    if (udp_ipv4_is_broadcast(to->address) && !socket->config.broadcast) {
        return UDP_ERR_INVALID_ARG;
    }
    return send_datagram(socket, to->address, to->port, data, length);
}

udp_result_t udp_socket_broadcast(udp_socket_t *socket, uint16_t port,
                                  const void *data, size_t length)
{
    if (socket == NULL || !socket->initialised || (data == NULL && length != 0)) {
        return UDP_ERR_INVALID_ARG;
    }
    if (!socket->open) {
        return UDP_ERR_NOT_OPEN;
    }
    if (!socket->config.broadcast) {
        /* Reaching every host on the network is worth asking for on purpose. */
        return UDP_ERR_INVALID_ARG;
    }

    static const uint8_t everyone[4] = { 255u, 255u, 255u, 255u };
    return send_datagram(socket, everyone, port, data, length);
}

uint16_t udp_socket_local_port(const udp_socket_t *socket)
{
    if (socket == NULL || !socket->initialised || socket->pcb == NULL) {
        return 0;
    }
    return pcb_of(socket)->local_port;
}

#endif /* UDP_SUPPORTED */
