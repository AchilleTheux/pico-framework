# ws2812_test

Hardware test for the `ws2812` component. Hardware tests are manual; this file
is the procedure (DESIGN_DOC.md section 19).

## Required hardware

* Any RP2040 or RP2350 board.
* A WS2812B strip or ring.
* A supply for the strip. The Pico's 3V3 rail can carry a few LEDs at low
  brightness; anything more needs an external 5 V supply with its ground tied
  to the board's ground.

## Wiring

| Strip | Board |
|-------|-------|
| DIN   | GPIO 10 (default; set by the profile) |
| GND   | GND — shared with the strip's supply |
| 5V    | strip supply, *not* the Pico's 3V3 for a long strip |

The default profile assumes 16 LEDs on GPIO 10. `long_strip` assumes 50 LEDs on
GPIO 11. To match a different bench setup, add a profile under
`profiles/tests/ws2812_test/` rather than editing the source.

## Running

```bash
make BOARD=pico2 APP=tests/ws2812_test PROFILE=default
make BOARD=pico2 APP=tests/ws2812_test PROFILE=default flash
```

Then open the console — USB CDC or the board's default UART at 115200:

```bash
picocom -b 115200 /dev/ttyACM0
```

The firmware narrates each step, then loops.

## Expected result

| Step | Expected |
|------|----------|
| solid colours | whole strip red, then green, then blue, then white, 1.5 s each |
| walking pixel | one white LED travelling from the end nearest DIN to the far end |
| brightness ramp | white strip fading smoothly up from off and back down, no flicker or stepping at the low end |
| rainbow | a hue gradient sweeping along the strip, wrapping smoothly with no seam |

Between passes the strip goes dark for a second.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| red and green swapped | strip is a clone with a different channel order; not an encoding bug — the wire order is pinned by the host tests |
| first few LEDs correct, rest garbage | 3.3 V data line too weak — add a level shifter, or shorten the lead to DIN |
| whole strip dark, console shows `ws2812_init failed` | no free state machine or no program space on `pio0` |
| whole strip dark, console otherwise normal | wiring — check DIN, and that grounds are common |
| flicker at high brightness | supply sag; power the strip separately |
| walking pixel runs the wrong way | expected — it starts at the end nearest DIN |
| fewer or more LEDs lit than expected | `WS2812_TEST_LENGTH` does not match the strip; pick or add a profile |
