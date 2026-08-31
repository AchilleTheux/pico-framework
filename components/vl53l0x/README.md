# vl53l0x

ST VL53L0X time-of-flight distance sensor, over [`i2c_device`](../i2c_device/).

## Why it is a component

You use it across projects, §18 lists "I2C device drivers" as a thing the
framework should grow, and it sits exactly on the layer `i2c_device` was built
to provide.

It also came out a fraction of the size of the version it was ported from —
about 400 lines against 1095. Not because anything was left out: the original
had to fold the whole start-up into a seven-hundred-line state machine
(`VL53L0X_Init_Loop_State`) **because its I2C layer was queued and
asynchronous**. With a blocking `i2c_device` the same sequence is a straight
line.

## Usage

```c
/* The application owns the bus; several devices share it. */
i2c_init(i2c0, 400000);
gpio_set_function(SDA, GPIO_FUNC_I2C);
gpio_set_function(SCL, GPIO_FUNC_I2C);

vl53l0x_t sensor;
if (vl53l0x_init(&sensor, i2c0, VL53L0X_DEFAULT_ADDRESS) != VL53L0X_OK) {
    /* nothing there, or not a VL53L0X */
}
vl53l0x_apply_profile(&sensor, VL53L0X_PROFILE_LONG_RANGE);
```

Then either block for a reading:

```c
uint16_t mm;
switch (vl53l0x_read_single(&sensor, &mm)) {
    case VL53L0X_OK:                 /* mm is a distance */
    case VL53L0X_ERR_OUT_OF_RANGE:   /* valid measurement, nothing in range */
    default:                         /* something went wrong */
}
```

or, for a robot, don't:

```c
vl53l0x_start_continuous(&sensor, 0);

/* in the main loop */
if (vl53l0x_data_ready(&sensor)) {
    uint16_t mm;
    vl53l0x_read_continuous(&sensor, &mm);
}
```

A measurement takes tens of milliseconds and nothing should stop for that. The
continuous path never blocks, and needs no state machine because `data_ready()`
is one register read.

## Two mistakes it will not let you make

**Out of range is not a distance.** The sensor reports **8190 mm** when nothing
is in range — a value that is perfectly plausible and would be read as a wall
eight metres away. `VL53L0X_ERR_OUT_OF_RANGE` is a separate result from both
success and failure, and the range status is checked before the distance is
believed.

**Wrong device is caught at start-up.** `init()` reads the identification
register and refuses unless it reads `0xEE`. A wrong address, or a different
device answering, fails there rather than producing nonsense measurements
afterwards.

## Profiles

| Profile | Budget | |
|---|---|---|
| `DEFAULT` | 33 ms | the sensor's own default, about 1.2 m |
| `FAST` | 20 ms | noisier and shorter, for a quick control loop |
| `ACCURATE` | 200 ms | quieter, and slow enough to notice |
| `LONG_RANGE` | 33 ms | longer VCSEL periods and a lower signal threshold: about 2 m, with more false readings off dark or angled surfaces |

Underneath, `vl53l0x_set_timing_budget()`, `set_signal_rate_limit()` and
`set_vcsel_periods()` are all available. The signal rate limit is in the
register's own unit of 1/128 MCPS rather than a float, so no floating point is
involved anywhere in the component.

Setting a VCSEL period re-applies the budget automatically. It has to: the
period changes what a macro clock is worth, so leaving the budget alone would
silently change how long the sensor measures for.

## Several sensors on one bus

Every VL53L0X wakes at **0x29**, so more than one needs each brought up alone
and moved:

1. Hold every sensor in reset with its XSHUT pin.
2. Release one, `vl53l0x_init()` it at 0x29, `vl53l0x_set_address()` it
   somewhere else.
3. Release the next.

The new address takes effect immediately and **does not survive a power cycle**,
so this has to be redone at every start-up. `vl53l0x_set_address()` updates the
handle before returning and re-probes at the new address, so a failure is
reported rather than leaving a handle pointing at silence.

## The timing arithmetic

The measurement budget is shared across ranging phases, each with a fixed
overhead and a timeout in a compressed format. That arithmetic is in
`vl53l0x_timing.c`, has no SDK dependency, and is host-tested — because it is
where drivers for this part actually go wrong, and nothing gives feedback when
it is wrong: the sensor just reads short, or times out, or drifts.

Two properties worth knowing, both tested:

* **The compressed timeout format is lossy downward, by up to 1/128.** Shifting
  stops as soon as the mantissa fits a byte, so its top bit is always set and
  the step between representable values is 1/128 of the value, not 1/256. Losing
  it downward is the safe direction: a shorter timeout ends a measurement early,
  where a longer one would overrun the budget.
* **A budget that cannot be met is refused, not clamped.** A budget quietly
  ignored is indistinguishable from one honoured, until the readings are wrong.

## Status

**Untested on hardware.** No sensor has been on the end of this.

The register sequence is reproduced from your working `Pamis_2026` driver rather
than written from memory, which is the main reason to have some confidence in
it. What has *not* been checked is the reorganisation around it: the sequence is
now linear rather than a state machine, the timeouts are read back through
different code, and continuous mode is polled rather than driven by the original
loop. Those are exactly the parts a first bring-up would exercise.

The magic register numbers are left as numbers rather than given invented names.
ST never published what most of them do, and a name that guesses is worse than
an address that admits it does not know.

## Testing

* Host: `make test` covers the timing arithmetic — VCSEL periods, the compressed
  timeout format, the microsecond conversions, and the budget.
* Hardware: `make APP=tests/i2c_test flash`, then `scan` to find the sensor and
  `tof`, `tofprofile`, `tofread`, `tofwatch` to use it. `tofread 100` reports
  the mean and spread, which is how you tell a working sensor from a noisy one.
