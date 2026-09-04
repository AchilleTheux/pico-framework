# tcp

A poll-driven TCP client over lwIP's raw API: connect out to a host and port,
send and receive bytes, reconnect when the link or the peer goes away.

```c
#include "tcp.h"

static uint8_t rx[1024];
static uint8_t tx[1024];
static tcp_client_t client;

tcp_client_init(&client);

const tcp_client_config_t config = {
    .host = "192.168.1.31",
    .port = 5000,
    .rx_buffer = rx, .rx_buffer_size = sizeof(rx),
    .tx_buffer = tx, .tx_buffer_size = sizeof(tx),
    .auto_reconnect = true,
};
tcp_client_open(&client, &config);

while (true) {
    wifi_poll(&wifi);        /* this is what drives lwIP */
    tcp_client_poll(&client);

    if (tcp_client_is_connected(&client)) {
        tcp_client_write(&client, "ping\n", 5);
    }
    int byte;
    while ((byte = tcp_client_read(&client)) >= 0) {
        /* ... */
    }
}
```

## A client, not a server

It dials out; it does not listen. Accepting inbound connections is a different
job with different resource ownership — a listen PCB, a policy for how many
peers at once, and an answer for what happens to the second one — and belongs
in its own component when an application needs one. Section 18's rule applies:
components appear when there is a real reuse case, not in anticipation of one.

One connection per instance. Two connections means two `tcp_client_t`, each
with its own buffers, which is also how lwIP counts them: `MEMP_NUM_TCP_PCB` in
`lwipopts.h` is the ceiling.

## Non-blocking, like everything else here

`tcp_client_open()` starts a DNS lookup and a connection and returns at once.
`tcp_client_poll()` drives the retry timer, the connect timeout, and the
outgoing buffer, and must be called regularly — the same contract as
`wifi_poll()` and `mqtt_poll()`.

It does **not** poll the radio or lwIP itself. lwIP's core processing, which is
what resolves the lookup and moves the connection along, happens as a side
effect of `wifi_poll()`. Calling `tcp_client_poll()` without something polling
the link means a state machine with nothing to advance it.

Opening before the link is up is fine and expected: the attempt fails, the
backoff starts, and it connects when the network arrives.

## Reconnection reuses wifi's backoff, not a copy of it

`wifi_retry_t`, unchanged. "Wait longer after each failure, cap it, survive the
millisecond counter's wrap" is exactly the same decision for a TCP peer as for
an access point, and it is already host-tested in
[`wifi_policy`](../wifi/README.md). `mqtt` made the same choice for the same
reason.

`auto_reconnect` decides whether a closed connection is retried at all. A robot
that loses its control link usually wants it back; a client doing one request
and stopping does not.

## The two directions are not symmetric

This is the part worth reading before using the component.

**Output is buffered and may be dropped.** `tcp_client_write()` copies into the
caller's outgoing buffer and returns how much it took. What actually reaches
the wire happens then if lwIP has room, and in `tcp_client_poll()` or lwIP's
sent callback otherwise. When the buffer is full the **tail** of the write is
dropped and counted in `tcp_client_dropped()` — losing the start of a message
loses the context that made the rest of it mean anything. A growing drop count
means the buffer is too small for what the application writes between polls.

**Input is refused whole, never truncated.** When the receive buffer cannot
hold an arriving segment, the component declines it: `on_recv` returns `ERR_MEM`
without freeing the pbuf, which is lwIP's back-pressure contract. lwIP keeps
the segment in `pcb->refused_data` and offers it again later, so the receive
window closes, the peer stops sending, and the bytes arrive once the buffer has
drained. Nothing is lost.

That asymmetry is why there is no receive drop counter: there is nothing to
count. It also means the receive buffer's size is this connection's flow-control
window in practice — a small one costs throughput, not data.

## Buffered output does not survive a close

`tcp_client_close()` discards whatever is still queued. A caller that needs it
delivered waits for `tcp_client_pending()` to reach zero first. Received bytes,
by contrast, **do** survive: a server that answers and then closes is the
ordinary case, and its answer is still worth reading after the connection has
gone. A new connection clears them, so one session's bytes cannot appear at the
front of the next.

## Naming

`tcp.c` includes lwIP's `lwip/tcp.h`, whose raw API already owns very nearly
every verb this component would want: `tcp_new`, `tcp_connect`, `tcp_write`,
`tcp_close`, `tcp_abort`, `tcp_bind`, `tcp_listen`, `tcp_accept`, `tcp_recv`,
`tcp_sent`, `tcp_err`, `tcp_output`, `tcp_recved` — and `tcp_poll`, which is a
different thing again from a poll loop.

So the API is prefixed `tcp_client_` throughout, uniformly rather than only
where a collision bites, and the connect verb is `open` so that reading
`tcp_client_open()` beside lwIP's `tcp_connect()` in the same file cannot
mislead. [`mqtt`](../mqtt/README.md) made the same trade for the same reason.

## PCB lifetime, which is where this kind of code goes wrong

lwIP frees the PCB itself before calling the error callback. Touching it there
— including to close it — is a use-after-free. `on_error()` therefore drops the
pointer rather than releasing it, and every other path goes through
`release_pcb()`, which clears the callbacks first so that a late one cannot
arrive pointing at an instance that has moved on.

`tcp_close()` can fail for want of memory to send the FIN; `release_pcb()`
aborts in that case rather than leaking the PCB waiting for memory that may not
come — and reports that it did, because the two are not interchangeable inside
a callback. `tcp_abort()` frees the PCB immediately, and lwIP carries on using
it unless the callback returns `ERR_ABRT`. So the peer-closed path in
`on_recv()` returns `ERR_ABRT` or `ERR_OK` according to which happened, rather
than assuming the close succeeded.

## A connect timeout, because lwIP's is minutes

lwIP will spend a long time on a SYN nobody answers. `connect_timeout_ms`
(10 s by default) makes a wrong port or an absent host fail in a timeframe a
person debugging it will sit through. It covers the DNS lookup too.

## Boards without a radio

The component compiles for every board so it need not be conditionally
registered. Without a CYW43, `TCP_SUPPORTED` is 0 and every call returns
`TCP_ERR_UNSUPPORTED`. Check the macro rather than discovering it at runtime.

## Resource ownership

* One lwIP `struct tcp_pcb` while open, released by `tcp_client_close()` or
  `tcp_client_deinit()`.
* No pins, no PIO, no DMA, no IRQ, no timer of its own.
* Both buffers are caller-owned and must outlive the connection.
* `TCP_STREAM_SEGMENT_MAX` (512) bytes of stack during a flush, for the copy
  out of the ring into something the link can take as one pointer.

## What this cannot tell you

Host tests cover the buffering against a fake link. They say nothing about
whether lwIP's window behaves as expected on a congested network, whether the
`ERR_MEM` back-pressure path is reached in practice, or how the connect timeout
interacts with a real DNS server. Those need the bench.

## Testing

* Host: `make test` runs `tcp_stream_test` — 16 cases covering partial sends
  and stream ordering, a link that takes nothing, a link that claims more than
  it was offered, the room limit, flushing past one staging buffer, whole-
  segment refusal and re-delivery, and what each direction does across a
  disconnection.
* Hardware: `make BOARD=pico2_w APP=tests/tcp_test flash` — see
  [`tcp_test`](../../apps/tests/tcp_test/).

**Untested on hardware.** Every build in the CI matrix compiles, on both
architectures and on a board with no radio, but no connection has been made to
a real peer. The bench's procedure is written; nothing has run it.
