# pwm

One PWM output on a GPIO, set by a frequency you can name and either a duty
cycle or a pulse width in microseconds.

```c
#include "pwm.h"

static pwm_out_t led;

const pwm_out_config_t config = {
    .gpio = 10,
    .frequency_hz = 1000,
    .duty = 0,
    .start_enabled = true,
};
pwm_out_init(&led, &config);

pwm_out_set_duty_percent(&led, 40);
pwm_out_set_duty(&led, PWM_DUTY_MAX / 2);   /* resolution-independent */
```

and for a servo, where the pulse width is the specification and the frame is
just what carries it:

```c
static pwm_out_t servo;

const pwm_out_config_t config = { .gpio = 10, .frequency_hz = 50,
                                  .start_enabled = true };
pwm_out_init(&servo, &config);
pwm_out_set_pulse_us(&servo, 1500);         /* centre */
```

## Why this exists at all, when the SDK's PWM API is already fine

It is deliberately thin. `pwm_set_gpio_level()` and friends are clean, and
DESIGN_DOC.md section 2.1 says not to wrap an API that is.

What the SDK does *not* do is work out a divider and a wrap for a frequency, or
a level for a pulse width. That arithmetic — an 8.4 fixed-point divider, a
16-bit wrap, and the interaction between them and the duty resolution you are
left with — is what gets copied between projects and quietly mis-rounded.
[`pwm_timing.c`](pwm_timing.c) is that arithmetic, integer-only and host-tested;
[`pwm.c`](pwm.c) is the register writing around it.

So: use this to say "50 Hz, 1500 us". Use the SDK directly for a phase-correct
counter, a slice clocked from a GPIO input, or anything else in the datasheet's
PWM chapter — this component does not reach for those, and does not stop you.

## Slices are shared, which is the thing to know

Each slice has two channels, A and B, on consecutive GPIOs — 0 and 1, 2 and 3,
and so on. The two channels have **separate levels but one wrap and one
divider**, so they cannot run at different frequencies. On a robot that reads as
a servo twitching whenever an unrelated LED is dimmed, and it is a genuinely
hard afternoon if you do not already know to suspect it.

`pwm_out_init()` will not do it quietly. If the slice is already enabled with a
different wrap or divider, it returns `PWM_ERR_SLICE_BUSY` and changes nothing.

That check reads `pwm_hw` — the hardware's own registers — rather than a table
of active outputs this component maintains. No global state, which the framework
prefers anyway, and a correct answer even for a slice left running by code that
is no longer there: a watchdog reboot with the motors still turning is exactly
the case a table would get wrong.

`pwm_gpio_shares_slice(a, b)` answers the same question before anything is
configured, which is where a board layout should be answering it.

Inversion is handled with the same care. The SDK's `pwm_set_output_polarity()`
writes both channels of a slice in one call, so setting one output's polarity
the obvious way would clear an inversion the other output had set. This
component reads the current bits back from `CSR` and replaces only its own.

`pwm_out_set_frequency()` is the one call that can still move another channel,
because that is what changing a shared wrap means. It is documented on the
function rather than prevented: this output's duty cycle survives (it is held as
a fraction and re-scaled), the other channel's does not (its level is a raw
count).

## Frequency is not always exactly what you asked for

The divider and wrap are integers, so only some frequencies divide the system
clock evenly. At 125 MHz:

| Asked | Divider | Counts per period | Achieved |
|-------|---------|-------------------|----------|
| 50 Hz | 38.1875 | 65466 | 50 Hz — 20000 us |
| 1 kHz | 1.9375 | 64516 | 1 kHz |
| 25 kHz | 1.0 | 5000 | 25 kHz exactly |
| 1 MHz | 1.0 | 125 | 1 MHz exactly |

`pwm_out_frequency()` reports what the slice actually runs at, and
`pwm_out_resolution()` the counts in a period — which is the number that matters
and the one nobody thinks to look at. At 1 kHz there are 64516 of them; at
1 MHz, 125, so a duty cycle can only be placed to about 0.8%. Above a few
hundred kilohertz, "50%" starts meaning something approximate.

**The divider is always the smallest the wrap allows**, because every halving of
it doubles the counts in a period. A frequency reached with a needlessly large
divider is correct and has thrown away most of its resolution, and nothing
downstream would say so.

## Reachable range

Bounded at both ends by the same registers. At 125 MHz:

* **Slowest: about 8 Hz.** The divider tops out at 255.9375 and the wrap at 16
  bits. Below that, `PWM_ERR_FREQUENCY_TOO_LOW`.
* **Fastest: the system clock.** Above that there is less than one count in a
  period, which is not a PWM signal; `PWM_ERR_FREQUENCY_TOO_HIGH`.

Both are refused rather than clamped. A silently clamped frequency is a bug that
surfaces on an oscilloscope weeks later.

## Duty cycle in 1/65535ths, not in counts

`pwm_out_set_duty()` takes a fraction of the period, so a caller need not know
what wrap its frequency happened to get, and changing the frequency does not
change the brightness. 0 is fully off and `PWM_DUTY_MAX` fully on **at every
frequency** — which sounds obvious and is the reason for one deliberate
compromise:

The hardware's wrap is a full `uint16`, so 65536 counts are reachable. But a
level is also a `uint16`, and "always high" needs a level *above* the wrap — at
`TOP = 65535` there is no such value, and full duty would come out as 65535/65536
rather than a solid line. So `PWM_TIMING_MAX_PERIOD_COUNTS` is 65535, one short
of the hardware maximum. One count of resolution buys `PWM_DUTY_MAX` meaning
what it says.

`pwm_out_set_duty_percent()` is there because a percentage is usually what the
caller actually has. It refuses above 100 rather than clamping — that is a
mistake, not an intention.

## Pulse width for servos and ESCs

`pwm_out_set_pulse_us()` sets the high time directly, which is how every RC
servo and ESC is specified: 1000–2000 us inside a 20 ms frame, where the frame
matters only for being roughly 50 Hz. Working in duty cycle instead means
recomputing 7.5% every time the frequency moves.

A pulse longer than the frame is clamped to a solid line rather than wrapped. On
an ESC the difference between those two is full throttle and none.

## Resource ownership

* One PWM slice, shared with the paired GPIO — see above.
* One GPIO, set to `GPIO_FUNC_PWM` at init and returned to SIO driven low by
  `pwm_out_deinit()`.
* `pwm_out_deinit()` stops the slice **only** if the paired GPIO is not also in
  PWM mode, since stopping it would stop that output too.
* No DMA, no IRQ, no PIO. The slice free-runs once enabled; nothing here needs
  polling.
* The system clock is sampled at init. A caller that changes `clk_sys`
  afterwards has changed every frequency here and must re-init — the SDK offers
  no callback to notice it.

## What this cannot tell you

Host tests cover the arithmetic exactly. They say nothing about what the pin
actually does: whether the frequency on a scope matches the computed one,
whether a fade looks smooth to an eye, whether a servo centres at 1500 us, or
whether the slice-conflict check fires against real hardware registers rather
than the reasoning above.

## Testing

* Host: `make test` runs `pwm_timing_test` — 14 cases covering the 50 Hz servo
  frame on both a 125 MHz and a 150 MHz clock, exact frequencies, the divider
  being minimised, both ends of the reachable range, a sweep from 10 Hz to
  100 kHz checked to within 0.2%, duty monotonicity and its endpoints, the
  `uint16` level guarantee at the longest period, and servo pulse widths against
  the numbers a datasheet gives.
* Hardware: `make BOARD=pico APP=tests/pwm_test PROFILE=onboard_led flash` —
  see [`pwm_test`](../../apps/tests/pwm_test/), which needs no wiring in that
  profile.

**Untested on hardware.** Every profile builds warning-free on both
architectures in the CI matrix, but no pin has been measured. The bench's
procedure is written; nothing has run it.
