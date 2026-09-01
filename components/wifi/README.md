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
(`lib/cyw43-driver/src/cyw43_ll.c`), which busy-waits up to
`CYW43_IOCTL_TIMEOUT_US` (500 ms, vendored default) per call.

Measured on a Pico 2 W running `wifi_test` (2026-09-01): with the configured
access point genuinely unreachable, the console went completely unresponsive
— no command produced output, and a single `ping` write eventually blocked
for over ten minutes — before recovering on its own the moment the access
point came back. A few hundred-millisecond ioctl timeouts do not by
themselves explain a stall that long, so something in the join retry path
gets stuck harder than a single `CYW43_IOCTL_TIMEOUT_US` wait once the AP has
been gone for a while; this has not been root-caused further.

Because `cli_poll()` and `wifi_poll()` share one loop in every application
here (per DESIGN_DOC.md §2.1, this component does not run its own thread or
core), that stall reaches the console too — on a robot, exactly when losing
the network is the thing you'd want to debug. This lives in the vendored,
pinned `cyw43-driver`/`pico_cyw43_arch` code, not in `wifi.c`, which already
uses the async join call as documented above. A real fix needs `wifi_poll()`
to run somewhere the rest of the application can't be blocked by it — e.g. a
dedicated core — which is a bigger change than this component's current
single-threaded contract; it has not been made. Until then, do not assume the
console stays responsive while a configured AP is unreachable.

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
