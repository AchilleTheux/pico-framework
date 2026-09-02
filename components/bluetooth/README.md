# bluetooth

A serial console over Classic Bluetooth: an SPP server that presents itself as a
`cli_stream_t`.

## What it gives you

The framework's CLI, over Bluetooth, with **no change to a single command**. On
a laptop the board appears as an ordinary serial port — `/dev/rfcomm0` on Linux,
a COM port on Windows — so any terminal program works and nothing special is
needed on the host.

```c
/* The only line that differs from a CLI over USB or a UART. */
const cli_config_t config = {
    .commands = commands,
    .command_count = count,
    .stream = bt_console_stream(&console),   /* <-- */
    /* ... everything else identical ... */
};
```

That is what the transport split in [`cli`](../cli/) was for, and this is the
third backend to use it after stdio and a raw UART.

## Why Classic and not BLE

A BLE equivalent needs a custom GATT service and an application on the host that
knows how to speak it. Classic SPP **is** a serial port, which is what a console
is. If you want telemetry to a phone app later, BLE is the right answer for
that — and a different component.

## The flow control, which is the interesting part

RFCOMM is not a byte stream. It carries packets, and it will only accept one when
the peer has granted credit, so a send at an arbitrary moment is *refused* rather
than queued. A CLI writes whenever it has something to say.

`bt_stream.c` is the piece in between, and it is free of BTstack and of the SDK
so the whole of it is host-tested against a fake link — the only way that logic
gets exercised without pairing a laptop and provoking each case by hand.

Decisions it makes, all tested:

| | |
|---|---|
| Writes are buffered until the link offers to send | there is no alternative; the send would simply fail |
| Several writes coalesce into one packet | RFCOMM has real per-packet overhead and a console writes in small pieces |
| Bytes the link refuses stay queued, **in place** | anything dropped there would be a hole in the middle of a reply, and anything requeued would be a transposition — see below |
| **A full buffer drops the tail, not the head** | losing the start of a reply loses the context that made it mean anything; losing the end merely truncates it |
| Dropped bytes are counted | a console that silently truncates is worse than one that admits it, and a growing count means the buffer is too small for what the firmware prints |
| Nothing is buffered with no peer attached | it would fill with output nobody asked for and leave no room for the next peer's reply |
| A peer leaving discards both buffers | stale text would otherwise be the first thing the next peer sees |

A write also *asks* for a send opportunity rather than waiting for the next
poll, so a reply does not sit in a buffer until the main loop comes round.

### A partial send is not a requeue

`bt_stream_send_fn` returns how many bytes the link took, which may be fewer
than it was offered. The refused suffix cannot simply be written back: a ring
buffer has no push-front, so it would land *behind* whatever was still queued
after the packet. Accepting `ab` out of an `abcd` packet, with `efghij` still
waiting, would put `abefghijcd` on the wire — not a truncated reply but a
transposed one, which reads as corruption rather than as loss.

So nothing is dequeued until the link has said how much it took:
`ring_buffer_peek_bytes()` copies the packet out without consuming it, and
`ring_buffer_discard()` then drops exactly the accepted prefix. A count larger
than the packet is clamped, so a link that overstates cannot pull unsent bytes
out with it.

`bt_console.c`'s `rfcomm_send` is all-or-nothing, so this path is not reachable
through it today; the contract is the API's, not that one caller's.

## Usage

```c
static uint8_t incoming[256];
static uint8_t outgoing[2048];      /* output wants the larger of the two */
static bt_console_t console;

const bt_console_config_t config = {
    .name = "pami-3",
    .incoming = incoming, .incoming_size = sizeof(incoming),
    .outgoing = outgoing, .outgoing_size = sizeof(outgoing),
    .discoverable = true,
};
bt_console_init(&console, &config);

while (true) {
    bt_console_poll(&console);
    cli_poll(&cli);
}
```

Output gets the larger buffer because a `help` listing is a few hundred bytes and
is produced far faster than RFCOMM will take it.

## Alongside WiFi

Both halves of the radio share one CYW43 and one async context, and **only one
`cyw43_arch` may be linked** — two would define `cyw43_arch_init()` twice. So the
choice is made once, in CMake:

| `PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI` | |
|---|---|
| `OFF` (default) | Bluetooth alone. This component links `pico_cyw43_arch_poll` and sets `CYW43_LWIP=0`, without which the cyw43 driver's header pulls in lwIP. |
| `ON` | The [`wifi`](../wifi/) component provides the arch and the `lwipopts.h`. This component depends on it, because a static library sees only its own dependencies and needs those headers to compile. |

Both are built in CI. Together they come to about 398 KB of flash.

## What it costs

| | flash |
|---|---|
| `bt_console_test` on `pico2` (no radio) | 35 KB |
| on `pico2_w`, Bluetooth alone | **369 KB** |
| on `pico2_w`, with WiFi as well | **398 KB** |

Most of it is the CYW43 firmware blob plus BTstack. It fits in the 960 KB
application region, but it is not a small addition.

## btstack_config.h

BTstack will not compile without one and the SDK does not supply it — the same
arrangement as lwIP's `lwipopts.h`. The component provides one configured for
exactly this: Classic, RFCOMM and SPP, one connection, no BLE and no audio.
Linking the component is enough.

Two of its settings, `HCI_OUTGOING_PRE_BUFFER_SIZE` and
`HCI_ACL_CHUNK_SIZE_ALIGNMENT`, are required by the CYW43 HCI transport and are
a `#error` if missing. Nobody should have to discover that.

## Pairing, and a warning about it

Secure Simple Pairing is enabled, and the "just works" association is accepted
without comparing a number — a robot has no screen to show one on. **That means
pairing authenticates nothing.** Anyone in range during the pairing window can
associate and then has a full console: every command, including `reboot`,
`bootsel`, and whatever an application adds.

For a competition robot that is usually an acceptable trade, and the mitigation
is `discoverable = false` once paired — a host it has paired with before still
connects, and it stops announcing itself to a hall full of other people's
laptops. If a console needs to be genuinely protected, that has to be a layer
above this component, and there is not one yet.

## Not done

* **BLE.** Compiled out, and a different component if it is ever wanted.
* **More than one peer.** BTstack's packet handlers are plain functions with no
  user pointer, so a second console instance could not be reached. The single
  instance is explicit rather than pretended otherwise by an API taking a handle
  it cannot use.
* **Anything but a console.** No file transfer, no audio, no HID.

## Status

Paired and exercised on a Pico 2 W against a Linux host (2026-09-01): Secure
Simple Pairing "just works" with no PIN prompt, the SDP record is read
correctly (the host resolves the Serial Port Profile UUID and offers a serial
port), `RFCOMM_EVENT_CAN_SEND_NOW` arrives and drains the outgoing buffer as
expected, and a departed peer's leftover buffered text does not leak into the
next session. `bt_console.c`/`bt_stream.c` themselves needed no changes.

The bug that first appearance found was one level up, in the test
application: see `apps/tests/bt_console_test/README.md`'s note on `flood`
originally dropping nearly everything because nothing let real time pass for
an HCI round trip between prints. Fixed there, not here — this component's
flow control did exactly what `bt_stream_on_can_send`'s contract says once
given the chance to run.

Re-validated on the same board (2026-09-03) after the partial-send fix above:
`flood 200` delivered all 200 lines in order with nothing dropped, and
`flood 500` — past what the 2 KB buffer holds — delivered 472 of 500, every
line intact, line numbers strictly increasing, and the 1673 missing bytes
reported by `btstatus` rather than swallowed. Dropped tail, never reordered,
which is what the table above promises.

## Testing

* Host: `make test` covers the buffering, the flow control, a partially
  accepted packet with more still queued behind it, full buffers in both
  directions, and a long session that wraps the ring buffers many times.
* Hardware: `make BOARD=pico2_w APP=tests/bt_console_test flash`. See that
  application's README for pairing and what to try.
