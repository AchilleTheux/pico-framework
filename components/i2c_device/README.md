# i2c_device

A device on an I2C bus, at an address.

The SDK's `i2c_write_blocking()` and `i2c_read_blocking()` are perfectly usable,
so this is not a wrapper around them. What it adds is the thing every device
driver otherwise reimplements: **reading and writing registers**.

Nearly every I2C peripheral works the same way — write a register address, then
read or write its contents — and nearly every driver for one writes its own
version of that, with its own byte order and its own idea of a timeout. Written
once, a driver for a new sensor becomes a register map and some conversions
rather than a transport.

## Byte order, and why it is the opposite of the servos'

Nearly every I2C sensor sends the **high byte first**. The servos on the other
side of this framework send the **low byte first**. Having both in one framework
is exactly how one gets used for the other, so both are explicit and both are
tested.

`I2C_ENDIAN_BIG` is the zero value, so a zero-initialised configuration is right
for the common case rather than quietly wrong. Getting it wrong does not fail —
it returns a number in range that looks like a plausible measurement.

## Usage

```c
/* The application owns the bus: several devices share it. */
i2c_init(i2c0, 400000);
gpio_set_function(SDA, GPIO_FUNC_I2C);
gpio_set_function(SCL, GPIO_FUNC_I2C);

i2c_device_t sensor;
i2c_device_init(&sensor, i2c0, 0x68, I2C_ENDIAN_BIG, 0);

if (!i2c_device_present(&sensor)) { /* nothing at that address */ }

uint32_t who;
i2c_device_read_value(&sensor, 0x75, 1, &who);      /* one byte */

uint32_t raw;
i2c_device_read_value(&sensor, 0x3B, 2, &raw);      /* two, high byte first */
int32_t acceleration = i2c_sign_extend(raw, 16);    /* it is signed */
```

| | |
|---|---|
| 8-bit register addresses | `i2c_device_read_bytes`, `write_bytes`, `read_value`, `write_value` |
| 16-bit register addresses | `..._bytes16`, `read_value16` — common on time-of-flight and camera parts |
| raw transfers | `i2c_device_write`, `read`, `write_read` — for devices that do not follow the register pattern |
| discovery | `i2c_bus_scan`, `i2c_device_present` |

Widths of 1 to 4 bytes are supported. Three is included deliberately: 24-bit
ADCs and pressure sensors are common, and a component that only did 1, 2 and 4
would send their drivers back to assembling bytes by hand.

`i2c_sign_extend()` is there because sensors report signed quantities in fields
narrower than a register. A 12-bit accelerometer reading of −1 read unsigned is
4095, which looks like a large positive acceleration.

## Two details that matter

**A register read holds the bus between the two halves.** `write_read()` passes
`nostop` on the write, so no stop condition is generated in the middle. One
there would release the bus, and another master — or a delayed repeated start —
could leave the device pointing at a different register than the one just
selected.

**A NACK and a timeout are different errors.** `I2C_DEVICE_ERR_NO_DEVICE` means
nothing is at that address; `I2C_DEVICE_ERR_TIMEOUT` means the bus itself is
stuck, usually a missing pull-up or a device holding SDA down. Collapsing them
into one error is how an afternoon gets spent on the wrong problem.

## Pull-ups

I2C needs them, and the pads' internal ones are around 50 kΩ — far weaker than
the specification wants. Enough for a short lead at 100 kHz, and no substitute
for 4.7 kΩ resistors on a real board. The test application can enable them, and
says which it is using.

## Status

The RP2040 I2C peripheral was configured as i2c1 on GPIO6/7 and completed an
empty-bus scan on an RP2040-Zero, confirming that the bus was not stuck. No
device acknowledged, so register transfers, byte order on the wire, and error
handling with a real peripheral remain unverified on hardware.

## Testing

* Host: `make test` covers byte order at every width and sign extension at the
  common field widths.
* Hardware: `make APP=tests/i2c_test flash` — an interactive bench with `scan`,
  `probe`, `use`, `read`, `write` and `dump`.
