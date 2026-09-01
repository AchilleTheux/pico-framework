# wifi

Station-mode connection management for the Pico W's CYW43 radio.

What DESIGN_DOC.md section 17 asks for: initialisation, connection management,
credentials, reconnect behaviour and status — with TCP and UDP kept separate, so
this component's job ends at *"there is a working link and here is its address"*.

## Non-blocking, like everything else here

Associating takes **seconds**, and on a robot that is seconds nothing else can
afford to stop for. `wifi_poll()` drives both lwIP and the connection state
machine and returns at once:

```c
wifi_t wifi;
wifi_init(&wifi);                    /* uploads the radio firmware; blocks briefly */

const wifi_config_t config = {
    .ssid = ssid, .password = password, .hostname = "pami-3",
    .retry = { .first_delay_ms = 1000, .max_delay_ms = 15000, .max_attempts = 0 },
};
wifi_connect(&wifi, &config);        /* returns immediately */

while (true) {
    cli_poll(&cli);
    wifi_poll(&wifi);                /* associates, and reconnects, in the background */
    /* ... the control loop ... */
}
```

`wifi_poll()` is also what **reconnects**. A link that drops is noticed there
and re-established on the retry schedule with the caller doing nothing.

Built against `pico_cyw43_arch_lwip_poll` rather than the threadsafe-background
variant. The cost is that a caller must poll; the benefit is that the stack
never runs in interrupt context and there is no concurrency to reason about —
the same trade every other component here makes.

## Credentials are never compiled in

The firmware this framework draws on had

```c
char Wifi_Name[50] = "wifi_5GHZ";
char Wifi_Pwd[50] = "MDP";
```

in its source. §13 forbids exactly that. `wifi_config_t` borrows pointers, so a
caller supplies them from wherever it likes; the framework's answer is
[`persistent_config`](../persistent_config/), set once over the console. That is
deliberately **not** a dependency of this component — `wifi_test` wires the two
together, and an application is free to do something else.

The SSID and passphrase are checked against what WPA2 allows *before* the radio
is asked, so a typo is reported as a bad passphrase rather than as a network
that will not associate. Those are very different things to chase. A 64-character
value is refused too: that is a hex PSK, not a passphrase, and passing one where
a passphrase is expected associates with nothing and says nothing.

## Retry

Exponential backoff with a ceiling, in `wifi_policy.c` — free of the SDK and
host-tested, which matters because the alternative is testing reconnection by
unplugging an access point and waiting.

`max_attempts` of 0 means never give up, which is what a robot wants.

Two properties worth naming, both tested:

* **The delay cannot overflow back to something tiny.** Doubling by a shift
  would, after enough failures, roll over to a very small delay and turn polite
  backoff into a flood. A robot left overnight against a dead access point
  reaches that point.
* **A retry scheduled across the millisecond counter's wrap still becomes due.**
  The counter wraps every 49.7 days; a direct comparison would, once per wrap,
  decide the next attempt was 49 days away and leave a robot offline until it
  was power-cycled. The comparison is a signed difference. This is not something
  anyone would find by testing against a real access point.

## Boards without a radio

The component compiles for **every** board, so it need not be conditionally
registered and the build matrix stays uniform. Without a CYW43, `WIFI_SUPPORTED`
is 0 and every call returns `WIFI_ERR_UNSUPPORTED`. Check the macro rather than
finding out at runtime.

`bras_attrape_caisse` is a plain RP2040 and has no radio.

## What it costs

| | flash |
|---|---|
| `wifi_test` on `pico2` (no radio) | 44 KB |
| `wifi_test` on `pico2_w` | **339 KB** |

Nearly 300 KB of that is the CYW43 firmware blob, which is linked into the image
and uploaded to the radio at start-up. It fits comfortably in the 960 KB
application region the flash layout gives, but it is not a small addition and is
worth knowing before putting WiFi on a board that also wants a large image.

## lwipopts.h

lwIP will not compile without one and has no usable defaults of its own, so the
component supplies it: one station interface, DHCP, TCP and UDP, no sockets and
no threads. Linking the component is enough. An application needing different
settings puts its own `lwipopts.h` earlier on the include path.

## Not done

* **TCP and UDP.** Kept separate on purpose, per §17. This component gets a link
  and an address; what goes over it is another component's business.
* **Access point mode**, scanning, and static addressing.
* **An external AT-command WiFi module.** The `Pamis_2026` firmware uses one over
  UART, which works on boards with no radio. Nothing here assumes CYW43 in its
  interface, so a second backend is possible without churning callers.

Classic Bluetooth SPP is implemented by the sibling `bluetooth` component.
The `bt_console_test/with_wifi` profile builds both against one shared
`cyw43_arch`.

## A retry can block the whole application, not just this component

`wifi_poll()` calls `cyw43_arch_poll()` unconditionally, and `wifi_connect()`
reaches `cyw43_arch_wifi_connect_async()` — which, despite the name, calls
`cyw43_wifi_join()` **synchronously**; only the *outcome* of the join is
reported asynchronously through `cyw43_tcpip_link_status()`. Every exchange
with the radio underneath that goes through `cyw43_do_ioctl()`
(`lib/cyw43-driver/src/cyw43_ll.c`), which waits via
`CYW43_DO_IOCTL_WAIT`/`CYW43_SDPCM_SEND_COMMON_WAIT`
(`pico_cyw43_driver/include/cyw43_configport.h`) for up to
`CYW43_IOCTL_TIMEOUT_US` — 1 second on this SDK's Pico port, which overrides
the driver's own 500 ms fallback.

Measured on a Pico 2 W running `wifi_test` on **Pico SDK 2.3.0** (2026-09-01):
with the configured access point genuinely unreachable, the console went
completely unresponsive — no command produced output, and a single `ping`
write eventually blocked for over ten minutes — before recovering on its own
the moment the access point came back. A single `CYW43_IOCTL_TIMEOUT_US` wait
does not by itself explain a stall that long.

**Root cause, confirmed by an A/B rebuild against Pico SDK 2.2.0 the same
day**: `CYW43_DO_IOCTL_WAIT` resolves to
`cyw43_await_background_or_timeout_us()` → `async_context_wait_for_work_until()`
→ `sem_acquire_block_until()` → a hardware-alarm-driven `WFE` sleep. Pico SDK
2.3.0 has a reported RP2350 regression where a short timed wait like this can
go to sleep without its wakeup alarm actually armed, so the core only wakes on
some *later, unrelated* event — here, the CYW43 host-wake interrupt when the
AP reappears — rather than at its own deadline. That matches what was
observed exactly: no AP means no such interrupt, so the wait — and with it
`cli_poll()`, since it shares the same loop — never returns until the AP
comes back. Upstream: RP2350 alarm/sleep issue
[raspberrypi/pico-sdk#3078](https://github.com/raspberrypi/pico-sdk/issues/3078),
CYW43-specific backtrace through this exact call chain
[raspberrypi/pico-sdk#3148](https://github.com/raspberrypi/pico-sdk/issues/3148),
both tracked against SDK 2.3.1.

Rebuilding the identical `wifi_test` image against **Pico SDK 2.2.0**, with no
other change, and repeating the same AP-off test for 60+ seconds: the console
stayed continuously responsive throughout, `ping` answering within a second
every time and the `[wifi] connecting` / `waiting to retry` cycle ticking
normally the whole way. The stall did not reproduce at all on 2.2.0.

This is a vendored SDK regression, not a design flaw in `wifi.c` — which
already uses the async join call as intended — nor is it evidence that
`wifi_poll()` needs its own core; that was this document's original
(incorrect) conclusion before the SDK version was implicated. It has been
left as a documented caveat rather than fixed: pinning the project to 2.2.0
would be a project-wide change (every board and component, not just this
one) and hasn't been decided on, and a narrower fix — overriding just
`CYW43_DO_IOCTL_WAIT`/`CYW43_SDPCM_SEND_COMMON_WAIT` to a bounded
`busy_wait_us()` via cyw43-driver's `CYW43_CONFIG_FILE` override hook,
keeping 2.3.0 otherwise — was identified as viable but not attempted. Until
one of those happens, do not assume the console stays responsive while a
configured AP is unreachable, on this SDK version.

## Status

Association, address acquisition, RSSI, and reconnection after both a
console-driven `connect` and a power cycle (from stored credentials) have all
run successfully on a Pico 2 W. The retry attempt counter climbs as expected
while an access point is down. What is now confirmed *not* to hold under that
same condition is the "never blocks" claim above — see the blocking caveat.

## Testing

* Host: `make test` covers the retry schedule, the counter wraparound and the
  credential limits.
* Hardware: `make BOARD=pico2_w APP=tests/wifi_test flash`, then `ssid`,
  `password`, `save`, `connect`. Credentials survive a power cycle and it
  reconnects on its own. Turning the access point off and back on is the
  interesting test — expect the console itself to stop responding for part of
  that window; see the blocking caveat above.
