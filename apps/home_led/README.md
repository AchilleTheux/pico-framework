# home_led

A WS2812 strip driven as a Home Assistant light: discovered automatically over
MQTT, controlled from the app or from a USB console, and remembered across a
power cut.

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
| On a strip | nothing — no strip was available when this was written |
| Against Home Assistant | nothing — no instance was available |

The components underneath it *are* validated on hardware: `wifi` and `mqtt` on
a Pico 2 W, `ws2812`'s blocking path on an RP2040-Zero. What is unproven is
this application on top of them, and in particular anything an eye has to
judge — whether the fades look smooth, whether the dithering does what it is
meant to at low brightness, whether the effects are pleasant. Record results
here when a strip is to hand; see "What to check first" at the end.

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
led> on / off
led> bri 200                 brightness, 0..255
led> rgb 255 128 0
led> ct 370                  colour temperature in mireds, 153..500
led> effect                  list the effects
led> effect Twinkle
led> status                  the light, the strip, the link and the broker
led> announce                republish the discovery document
```

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

```text
main.c      the only file that touches the Pico SDK: settings, polling,
            rendering, and moving payloads between the broker and ha.c
light.c     the model -- power, brightness, RGB or colour temperature,
            effect, and the two fades
effects.c   what reaches the strip, per effect
ha.c        the Home Assistant topics, discovery document and JSON
```

`light`, `effects` and `ha` call no SDK function, so all three compile
directly into the host tests — `tests/apps/home_led_*_test.c`. That is the
same split `ws2812.c` and `ws2812_color.c` use, applied to an application: it
leaves `main.c` as the only part that needs hardware to judge, which matters a
great deal when there is no hardware to judge it with.

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

On boot they are restored with both fades already finished, and every field is
validated on the way in: this data comes from flash, and an effect index past
the end of the table would be read straight out of the name array.

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

1. `test` — the pattern appears, red at the near end. If the colours are
   swapped it is a clone with a different channel order, not a bug; if only
   the first few pixels are right, suspect the 3.3 V data line.
2. `status` — the LED count matches the strip.
3. `bri 10` then `bri 250` — the fade is smooth, with no visible step
   backwards, and the low end still shows gradations rather than a staircase.
   That low end is what the dithering is for.
4. Each effect in turn, at a middling brightness.
5. `connect`, then watch for `[ha] announced as ...` and check the light
   appears in Home Assistant.
6. Drive it from Home Assistant: on/off, the brightness slider, a colour, the
   colour-temperature slider, each effect.
7. Pull the power, restore it: the light comes back as it was, and Home
   Assistant shows it available again.

## Where this came from

A rewrite of `home-led`, an earlier standalone firmware for the same job. The
differences worth knowing about:

* That firmware declared two strips but only ever drove one — the second had
  no length and no pin, and every effect looped over `NB_RGB_STRIP - 1`. This
  drives one strip and uses the onboard LED for status instead.
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
