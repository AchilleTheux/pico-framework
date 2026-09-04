# udp

Poll-driven UDP datagrams over lwIP's raw API: bind a port, send to a host,
receive through a callback, and broadcast to a network you have not been told
anything about.

```c
#include "udp.h"

static udp_socket_t sock;

static void on_datagram(void *arg, const udp_endpoint_t *from,
                        const uint8_t *data, size_t length)
{
    char address[UDP_ADDRESS_LENGTH];
    udp_ipv4_format(from->address, address, sizeof(address));
    printf("%s:%u sent %u bytes\n", address, from->port, (unsigned)length);

    udp_socket_send_to_endpoint(&sock, from, "ack", 3);   /* safe from here */
}

udp_socket_init(&sock);

const udp_socket_config_t config = {
    .local_port = 5005,
    .broadcast = true,
    .on_datagram = on_datagram,
};
udp_socket_open(&sock, &config);

udp_socket_send_to(&sock, "192.168.1.31", 5005, "hello", 5);
udp_socket_broadcast(&sock, 5005, "who is there", 12);

while (true) {
    wifi_poll(&wifi);        /* datagrams arrive from inside here */
}
```

## What it is for, and what it is not

UDP is the answer to traffic TCP is wrong for: telemetry that is worth having
now or not at all, and discovery, where a device has to announce itself to a
network nobody has configured it for.

There is no connection, no retry, no ordering and no delivery guarantee here,
deliberately. A datagram is sent once and either arrives or does not. A caller
that needs more than that wants [`tcp`](../tcp/README.md).

## There is no udp_socket_poll()

This component has no state machine to drive. Datagrams arrive as a side effect
of whatever already polls the link — `wifi_poll()` — and the callback runs from
inside it. Sending is immediate rather than buffered, because there is no send
window to wait for: `udp_socket_send_to()` either hands the datagram to the
stack or says why it could not.

The callback runs on lwIP's receive path, so the same rule as `mqtt`'s
`on_message` applies: do the small thing there and leave the long one to the
main loop. `udp_socket_send_to_endpoint()` is safe to call from inside it, which
is what makes a request/response protocol a few lines.

## A strict address parser, and why not lwIP's

`udp_ipv4_parse()` takes exactly four decimal octets, no leading zeros, nothing
before or after. lwIP's `ipaddr_aton()` is `inet_aton()`'s, and `inet_aton()` is
generous in ways nobody wants from a configuration field:

| Input | `inet_aton()` | here |
|-------|---------------|------|
| `10.1` | 10.0.0.1 | rejected |
| `192.168.1.010` | 192.168.1.**8** — octal | rejected |
| `0x0a.1.1.1` | 10.1.1.1 — hexadecimal | rejected |
| `1.2.3.4.5` | varies | rejected |

A typo in a configured peer address should be a rejected setting, not a device
quietly talking to a different host. That is a much worse afternoon, and it is
the entire reason this parser exists rather than a call into lwIP.

A hostname is simply "not a dotted quad", so a `false` from the parser is the
caller's cue to try DNS rather than an error. `udp_socket_send_to()` does
exactly that.

## DNS, and the blunt contract around it

`udp_socket_send_to()` accepts a hostname, but DNS is not instant. When the name
is already in lwIP's cache the datagram goes at once; when it is not, the call
returns `UDP_ERR_RESOLVING` having started the lookup and **sent nothing**. Call
again in a moment and the answer will be there.

That is deliberately blunt, and it suits UDP: a datagram is already something
that may not arrive, so a caller is written to repeat itself anyway. A caller
that cannot tolerate it resolves once at startup and then uses
`udp_socket_send_to_endpoint()`, which needs no lookup — as does anything
replying to a datagram it just received, since the sender arrived with the
message.

## Broadcast, and what is not supported

`udp_socket_broadcast()` sends to 255.255.255.255, which reaches every host on
the local network without knowing the subnet — the useful property when a robot
is looking for a control station on somebody else's field network. It requires
`broadcast` in the configuration: reaching every host is worth asking for on
purpose, so without it the call is refused rather than quietly sent.

Only the *limited* broadcast address is recognised as broadcast. A subnet
broadcast such as 192.168.1.255 is one too, but this component does not know the
netmask and does not guess.

**Multicast is not supported.** Joining a group needs IGMP, which this build's
`lwipopts.h` does not enable, and enabling it would cost flash and RAM in every
`wifi` image for something nothing here has asked for yet. Sending to 224.0.0.0/4
is refused with `UDP_ERR_UNSUPPORTED_ADDRESS` rather than failing obscurely.

`SOF_BROADCAST` is set on the PCB when broadcast is enabled. With
`IP_SOF_BROADCAST` at lwIP's default of 0 the stack does not check that flag, so
a broadcast goes out either way; it is set because it records the intent where
the PCB can be inspected, and keeps this working if the option is ever turned
on.

## Size limits, in two places

`UDP_MAX_PAYLOAD` (1472) is what one datagram carries on an ordinary network:
1500 MTU less 20 bytes of IPv4 header and 8 of UDP. IP would fragment anything
larger and lwIP would need the pbufs to hold it, so a longer datagram is not a
slower datagram but a lost one. `udp_socket_send_to()` refuses it at the call.

`UDP_MAX_DATAGRAM` (512, overridable) is how much of an *incoming* datagram this
component will reassemble. A longer one is counted in
`udp_socket_datagrams_dropped()` rather than delivered truncated — the same view
`mqtt` takes of an oversized publish: a fragment handed to a caller expecting
the whole message is worse than no callback at all. It costs that many bytes of
RAM per socket.

## Naming

`udp.c` includes lwIP's `lwip/udp.h`, which already owns `udp_new`, `udp_bind`,
`udp_connect`, `udp_disconnect`, `udp_send`, `udp_sendto`, `udp_recv` and
`udp_remove` — every verb this component wants. The API is therefore prefixed
`udp_socket_` throughout, as [`mqtt`](../mqtt/README.md) and
[`tcp`](../tcp/README.md) do for the same reason.

"Socket" here means a bound local endpoint, not a BSD socket: `LWIP_SOCKET` is 0
in this build and there is no file descriptor anywhere near this.

## Boards without a radio

`UDP_SUPPORTED` tracks `WIFI_SUPPORTED`; without a CYW43 every call returns
`UDP_ERR_UNSUPPORTED`. Check the macro rather than discovering it at runtime.

## Resource ownership

* One lwIP `struct udp_pcb` while open, released by `udp_socket_close()` or
  `udp_socket_deinit()`.
* `UDP_MAX_DATAGRAM` bytes inside `udp_socket_t` for reassembly.
* No pins, no PIO, no DMA, no IRQ, no timer.

## What this cannot tell you

Host tests cover the parser, the classification and the bounds. They say nothing
about whether a broadcast actually reaches another host, whether an access point
forwards it, or whether the ephemeral port lwIP picks is reachable from
outside. Those need the bench.

## Testing

* Host: `make test` runs `udp_policy_test` — 15 cases covering the parser
  against every `inet_aton()` form it exists to reject, formatting and its
  round trip, broadcast and multicast classification, endpoint validation, and
  the payload bound.
* Hardware: `make BOARD=pico2_w APP=tests/udp_test flash` — see
  [`udp_test`](../../apps/tests/udp_test/).

**Untested on hardware.** Every build in the CI matrix compiles, on both
architectures and on a board with no radio, but no datagram has left a board.
The bench's procedure is written; nothing has run it.
