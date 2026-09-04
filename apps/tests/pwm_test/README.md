# pwm_test

Bench for the [`pwm`](../../../components/pwm/) component: a serial command line
onto one PWM output, plus the two checks that need a second one.

Hardware tests are manual; this file is the procedure (DESIGN_DOC.md section 19).

## Required hardware

Depends how far you want to go.

| Profile | Needs |
|---------|-------|
| `onboard_led` | **nothing** — GPIO 25 is the onboard LED of a Pico or Pico 2 |
| `default` | an LED and a resistor on GPIO 10, or an oscilloscope |
| `servo` | an RC servo, and its own 5–6 V supply |
| `motor` | an oscilloscope, or a motor driver you trust |

`onboard_led` is the one to start with: it proves the whole component with no
wiring at all. It is **not** for a Pico W or Pico 2 W, whose LED hangs off the
CYW43 rather than a bank 0 pin and cannot be driven by PWM.

### Wiring, for the profiles that need it

```text
  GPIO 10 ──── 330R ──── LED ──── GND          (default)

  GPIO 10 ─────────────── servo signal          (servo)
      GND ─────────────── servo ground ──── servo supply ground
                          servo power  ──── 5-6 V supply
```

A servo cannot run from the board's 3V3, and browning out the Pico through a
servo is the classic way to spend an afternoon suspecting the firmware.

An oscilloscope on GPIO 10 is worth more than any of this: the point of the
component is that the frequency and pulse width are what they claim, and only a
scope can say.

## Running

```bash
make BOARD=pico APP=tests/pwm_test PROFILE=onboard_led
make BOARD=pico APP=tests/pwm_test PROFILE=onboard_led flash
picocom -b 115200 /dev/ttyACM0
```

## Commands

| Command | Does |
|---------|------|
| `freq [hz]` | show or set the frequency |
| `duty [0-65535]` | show or set the duty cycle as a fraction |
| `pct <0-100>` | set the duty as a percentage |
| `pulse <us>` | set the high time directly |
| `on` / `off` | start and stop the counter |
| `fade` | one ramp up and down |
| `servo` | sweep 1000–2000–1500 us (needs 50 Hz) |
| `second` | start the paired GPIO at the same frequency |
| `conflict` | try the paired GPIO at a *different* frequency — must be refused |
| `status` | pin, slice, asked and achieved frequency, resolution, duty |

## The tests that matter

### 1. The frequency is what it says

`status` prints what was asked and what is running. On a scope, GPIO 10 must
match the "running" figure, not the "asked" one — and at 1 kHz they are the
same.

Then `freq 25000`: exact at 125 MHz, 5000 counts per period. Then `freq 3000000`:
still works, but `status` shows the resolution collapsing to a few dozen counts.
That is the trade the README describes, made visible.

### 2. A fade with no step backwards

`pct 50`, then `fade`. On an LED the ramp must be smooth in both directions with
no visible jump, and in particular no moment where it gets *darker* while
brightening. A step backwards would be non-monotonic duty arithmetic — pinned by
the host tests, but this is where an eye confirms it.

`pct 0` must be fully dark and `pct 100` fully lit, with no flicker at either
end. Those two are the `PWM_DUTY_MAX` guarantee: at 0 the level is 0, and at 100
it is one above the wrap.

### 3. Both ends are refused, not clamped

```text
pwm> freq 2
refused: too slow for the divider
pwm> freq 200000000
refused: too fast for this clock
```

The output must keep running at its previous frequency in both cases. A clamp
here — accepting the value and silently doing something else — is the failure
this component is meant to prevent.

### 4. Slice sharing, which is the interesting one

This is the check that needs no scope and proves the most.

```text
pwm> status                # note the slice number
pwm> second                # GPIO 11, same slice, same frequency
```

`second` should report that the two GPIOs share a slice and that the second
output started. Both LEDs (or both scope channels) now run at the same frequency
with different duty cycles — 25% on the second.

Then:

```text
pwm> conflict              # GPIO 11 at twice the frequency
```

This **must** print `slice is running at another frequency` and
`correct: the first output was left alone`. Confirm on the first output that it
did not change: its `status` frequency is what it was, and an LED on it does not
visibly shift.

If `conflict` succeeds instead, the hardware read-back check is wrong, and the
component's central safety claim is false.

### 5. A servo, in the units a servo is sold in

With `PROFILE=servo`:

```text
pwm> status                # 50 Hz, ~20000 us period, ~65466 counts
pwm> pulse 1500            # centre
pwm> pulse 1000            # one end
pwm> pulse 2000            # the other
pwm> servo                 # sweep between them
```

The servo must reach both ends and centre repeatably. On a scope the high time
must measure 1000/1500/2000 us to within a few microseconds — the component's
resolution here is about 0.3 us, so anything worse than that is an arithmetic
error rather than a rounding one.

`pulse 25000`, longer than the frame, must give a solid line rather than a short
pulse. A servo will not like it; do this one on a scope.

### 6. `off` leaves the pin where it stopped

`pct 50` then `off`: the counter stops and the pin holds whatever level it was
on at that instant, which may be high. That is the documented behaviour, not a
bug, and it is why anything driving a motor should set the duty to 0 before
stopping the slice. Worth seeing once so it is never a surprise.

## Expected result

| Step | Expect |
|------|--------|
| boot | `pwm_test board pico gpio 25`, asked/running/resolution |
| `pct 50` | LED at half brightness |
| `fade` | smooth ramp both ways, no step backwards |
| `pct 0` / `pct 100` | fully dark / fully lit, no flicker |
| `freq 25000` | `25000 Hz, 5000 counts per period` |
| `freq 2` | `refused: too slow for the divider` |
| `second` | shares a slice: `yes`, second output starts |
| `conflict` | `slice is running at another frequency`, first output unchanged |
| `pulse 1500` (servo) | servo centres; 1500 us on a scope |

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| nothing on the pin | wrong GPIO for the profile, or `off` |
| LED flickers at `pct 100` | the full-duty level is not above the wrap — the `PWM_TIMING_MAX_PERIOD_COUNTS` guarantee |
| fade has a visible step | non-monotonic duty rounding |
| frequency on the scope ≠ `status` | the divider or wrap write, not the arithmetic (the host tests cover that) |
| `conflict` succeeds | the `pwm_hw` read-back check is wrong |
| servo buzzes at the ends | usually the servo's own travel limits, not the pulse; check on a scope before blaming the firmware |
| board resets when the servo moves | the servo is drawing from the Pico. Give it its own supply |

## What this proves, and what it does not

It proves one output, and one pair sharing a slice. It says nothing about all
eight (or twelve) slices at once, about PWM under a changing system clock — the
component samples `clk_sys` at init and documents that it must be re-initialised
— or about phase-correct mode, which this component does not offer.

## Status

**Not yet run.** Every profile builds warning-free for `pico`, `pico2` and
`rp2040_zero` in the CI matrix. No pin has been measured. Record results here
when it is.
