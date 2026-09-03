# home_led

A WS2812 strip driven as a Home Assistant light: discovered automatically over
MQTT, controlled from the app or from a USB console, and configured from flash.
Every boot leaves the strip off, ready to turn on as solid 3000 K white.

```bash
make BOARD=pico2_w APP=home_led flash
```

## Status

**Not yet validated on hardware.** Everything below is what the firmware is
built to do; what has actually been demonstrated is:

| | |
|---|---|
| Host tests | 73, across the light model (25), the effects (20) and the Home Assistant schema (28), under ASan and UBSan |
| Cross-builds | `pico2_w`, `pico_w`, `pico2` (no radio), both profiles, warnings as errors |
| On a Pico 2 W | everything except the strip — see below |
| On a strip | the wire order and the primaries; the rest still unseen |
| Against Home Assistant | nothing — no instance was available; the MQTT exchange was driven by hand instead |

Validated on a Pico 2 W against `broker.hivemq.com`, 2026-09-03. Every part of
this that does not need LEDs:

* Boots, restores its network settings and preferred brightness across a
  reflash, starts the strip off at solid 3000 K, associates, and connects to
  the broker with no console involvement.
* Command history recalls previous lines with the arrow keys, erasing the
  current one properly, and re-runs a recalled line on Enter.
* A 382 KiB image transferred over the console, staged and verified against
  its CRC-32 — while the light carried on rendering — and then installed with
  `fwapply`, after which the board rebooted into it and came back configured.
* Five consecutive unpaced transfers at 2203–2205 records/s, each verifying,
  which is what the USB-CDC pacing default is based on.
* Publishes a 634-byte discovery document, its state, and `online` — all
  retained, all received intact by an independent subscriber. That document is
  past lwIP's 256-byte default output ring buffer, so it also exercises the
  override in `components/wifi/include/lwipopts.h`.
* Applies a full command from off-board — power, brightness, an RGB colour and
  an effect in one message — and republishes a state whose `color_mode` and
  colour attribute agree.
* Applies a **partial** command (`{"brightness":60}`) without disturbing the
  power, colour or effect. This is the behaviour a slider drag depends on.
* Reports an effect name it does not have, and refuses a payload that is not a
  command (`[1,2,3]`), rather than silently doing nothing.
* Saves to flash ten seconds after the last change and not before. On reboot,
  stored brightness is retained while power, effect and colour temperature
  return to off, Solid and 3000 K.
* The last will works end to end: `online` → `offline` when the link dropped
  → `online` on reconnect, which is what Home Assistant reads as availability.

On a 300-pixel WS2815 strip at 12 V behind a 5 V level shifter, same date: the
strip lights and `rgb 255 0 0`, `0 255 0` and `0 0 255` render as red, green
and blue — **once the wire order is set to RGB**. With the GRB default the
primaries came out swapped, which is what `APP_LED_ORDER` in the profiles is
now set for; see [`ws2812`'s README](../../components/ws2812/README.md) for
why two channels and not three.

What is still unproven is everything else an eye has to judge — whether the
fades look smooth, whether the dithering does what it is meant to at low
brightness, whether the effects are pleasant, whether all 300 pixels light —
and Home Assistant's own handling of the discovery document. Record those here
as they are seen; see "What to check first" at the end.

## Required hardware

* A board with a radio — `pico2_w` or `pico_w`. It builds for one without, and
  the console and the strip work, but there is no Home Assistant without a
  network.
* A WS2812 / WS2812B strip on the data pin the profile names (GPIO 6 by
  default).
* **Its own 5 V supply**, for anything past a handful of LEDs, with its ground
  tied to the board's. 300 pixels at full white is on the order of 18 A; the
  board's own 5 V pin will not do it.
* A level shifter is worth having. 3.3 V is below the WS2812B's specified
  logic high, and it is the usual cause of a long strip that corrupts after
  the first few pixels.

The `bench` profile drops the strip to eight LEDs, which runs from the board's
own 5 V pin and is enough to exercise everything except how a long strip
looks.

## Setting up, once

Settings live in flash, not in the source. Over USB serial:

```text
led> ssid my-network
led> password my-passphrase
led> broker 192.168.1.31
led> port 1883
led> mqttuser pico
led> mqttpass secret
led> deviceid living-room
led> save
led> connect
```

After that the board needs no console: it associates, connects to the broker,
publishes its discovery document, and Home Assistant shows a light called
"LED strip" under a device named after `deviceid`.

`haprefix` changes the discovery prefix if Home Assistant has been configured
away from `homeassistant`.

## Driving it from the console

Useful without a broker at all, and the quickest way to see whether the strip
is wired correctly:

```text
led> test                    toggle a wiring pattern: red at the first pixel,
                             green at the last, dim white between
led> order [GRB|RGB|...]     the strip's wire colour order, live
led> on / off
led> bri 200                 brightness, 0..255
led> rgb 255 128 0
led> ct 370                  colour temperature in mireds, 153..500
led> effect                  list the effects
led> effect Twinkle
led> status                  the light, the strip, the link and the broker
led> announce                republish the discovery document
```

Up and down recall previous commands: eight lines of history, sized separately
from the line buffer because that one has to hold a 521-character Intel HEX
record and eight slots of *that* would be 4.8 KiB spent remembering lines
nobody typed.

## Reflashing over the same console

The board carries the framework's serial updater, so a new image goes over the
console with no USB and no BOOTSEL — which is the point for a light in a fixed
installation, where the console is reachable when the board is not.

```bash
make BOARD=pico2_w APP=home_led flash-serial PORT=/dev/ttyACM0          # stage and verify
make BOARD=pico2_w APP=home_led flash-serial PORT=/dev/ttyACM0 APPLY=1  # and install it
```

`fwbegin` / `fwstatus` / `fwverify` are safe to run on any board: they only
touch the staging half of flash. `fwapply` is the one that overwrites the
running image and reboots, and the profiles here turn it on
(`FIRMWARE_SERVICE_ENABLE_APPLY`). Read
[`serial_update_test`](../tests/serial_update_test/README.md) before relying
on it.

Over USB that takes about twelve seconds for a 380 KiB image; the pacing that
a real UART needs is skipped, chosen from the port name. See the framework
README's "Flashing without the BOOTSEL button".

A transfer and an interactive session share the link without interfering:
records begin with `:`, which `raw_line_prefix` marks as not-for-human-eyes,
so an upload is neither echoed character by character nor recorded into
history.

## The effects

| | |
|---|---|
| `Solid` | one colour, the whole strip |
| `Rainbow` | a hue gradient along the strip, turning over about three seconds |
| `Twinkle` | sparks over a dim wash of the chosen colour |
| `Wipe` | a head with a short fading tail, running round the strip |
| `Breathing` | the whole strip rising and falling over eight seconds, never to black |

The names are a published interface — Home Assistant sends back whatever
string this device advertised — so renaming one breaks every controller that
learned the old list. `the_published_effect_list_is_exactly_what_the_firmware_accepts`
in the host tests holds the list and the parser together.

## How it is put together

Split by what each part *needs*, not by what it does:

```text
main.c      startup and the loop, and nothing else
app.h       the one context every module below is handed

light.c     the model -- power, brightness, RGB or colour temperature,
            effect, and the two fades          <- no SDK, host-tested
effects.c   what reaches the strip, per effect <- no SDK, host-tested
ha.c        the Home Assistant topics, discovery document and JSON
                                               <- no SDK, host-tested

settings.c  what survives a power cut, and the commands that set it
net.c       the link, the broker session, and the announcement
render.c    frames onto the strip, and keeps the onboard LED off
console.c   the command table and the interpreter
```

The first three call no SDK function, so all three compile directly into the
host tests — `tests/apps/home_led_*_test.c`. That is the same split `ws2812.c`
and `ws2812_color.c` use, applied to an application: it leaves the wiring as
the only part that needs hardware to judge.

The wiring shares one `app_t` rather than a drawer of file statics. Every CLI
command already receives a `user_data` pointer; that pointer is the app, so a
command reaches the light through its argument instead of through a static
that happens to be in scope — the same rule the components follow.

### Brightness, gamma and dithering

Three things happen to a colour between an effect choosing it and the wire,
and the order is not negotiable:

1. **The effect scales it for brightness, with dithering.** Dithering varies
   the rounding threshold per pixel and per frame, so it has to happen where
   the scaling happens. That is why `ws2812_set_brightness()` is left at 255
   here — the driver scales every pixel identically by design, which is
   exactly what dithering must not do.
2. **The driver applies gamma**, via `ws2812_set_gamma(&strip, ws2812_gamma_table)`.
   Gamma says what value produces a given apparent brightness, so scaling a
   corrected value undoes the correction: it has to be last.
3. **The driver packs it** into the strip's GRB wire order.

Get 1 and 2 the wrong way round and the light still works — it just fades
badly, in a way that is hard to attribute to anything.

### Fades

Brightness moves over three seconds and colour temperature over four, both
eased with smoothstep rather than linearly, because a linear fade has a
visible corner at each end. `light_tick()` advances them from the main loop
and nothing blocks.

The state published to Home Assistant is the value that was *asked for*, not
the one the fade has reached. Reporting the ramp would make a slider crawl for
three seconds after every change and fight whatever the user did next.

### Persistence

The light's own settings — power, brightness, colour, mode, effect — are
written to flash ten seconds after the last change, and only if something
actually changed. Home Assistant sends a message per step while a slider is
dragged; writing each one would be dozens of erase/program cycles for one
gesture. `light.generation` is what makes "actually changed" cheap to ask.

Boot has a deliberate safety override: it retains the stored brightness and
RGB preference, but starts off with the Solid effect in colour-temperature
mode at 333 mired (about 3000 K). Both fades start already finished. Thus an
older saved ON/Rainbow state cannot relight the strip during a reboot.

### Reconnecting

lwIP always connects with the clean-session flag set, so the broker forgets
this client's subscription the moment a session drops, and a restarted Home
Assistant sees only what is retained. Everything that has to be
re-established — the subscription, the discovery document, the availability
message, the current state — is done from `mqtt`'s `on_connect`, so it happens
on the first session and every later one with no separate bookkeeping.

A last-will on `<prefix>/light/<id>/status` is how Home Assistant learns about
a power cut: the broker publishes "offline" on the device's behalf.

## What to check first, when a strip is available

In this order, because each one rules out the causes of the next:

The console and network half is already done (see Status); what remains needs
the LEDs.

1. `test` — the pattern appears, red at the near end, and all of the strip
   lights. If the colours are swapped, that is the wire order: run `order` and
   try the six until red is red, then put the answer in the profile. If only
   the first few pixels are right, suspect the data line rather than the
   firmware.
2. `status` — the LED count matches the strip.
3. `bri 10` then `bri 250` — the fade is smooth, with no visible step
   backwards, and the low end still shows gradations rather than a staircase.
   That low end is what the dithering is for.
4. Each effect in turn, at a middling brightness.
5. Check the light appears in Home Assistant and that its dropdown offers the
   five effects. Everything up to the broker is already confirmed; what has
   never been seen is Home Assistant's own reading of the discovery document.
6. Drive it from Home Assistant: on/off, the brightness slider, a colour, the
   colour-temperature slider, each effect.

## Where this came from

A rewrite of `home-led`, an earlier standalone firmware for the same job. The
differences worth knowing about:

* That firmware declared two strips but only ever drove one — the second had
  no length and no pin, and every effect looped over `NB_RGB_STRIP - 1`. This
  drives one strip and explicitly keeps the onboard LED off.
* Its brightness scaling divided by 256 rather than 255, so full white never
  quite reached 255. `ws2812_color_scale()` rounds correctly.
* Its fade state machine kept an `int *` and a `uint8_t *` in one struct with
  a tag, and wrote four bytes through a pointer aimed at a `uint16_t`. The
  model here returns values instead.
* Its JSON parsing was a `strstr` for the key name, which matches a key
  appearing inside a *value* — an effect called "brightness test" would have
  been read as a brightness. The [`json`](../../components/json/) component
  matches real keys.
* Its dither inverted the Bayer pattern by permuting the matrix index, which
  moves the grain around instead of cancelling it.
* WiFi and MQTT credentials were compiled in. They are in flash here, set over
  the console, and never printed back.
