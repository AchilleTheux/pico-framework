# i2c_test

Interactive bench for the `i2c_device` component: a serial command line onto an
I2C bus.

*"Is the sensor even wired up"* is the first question on a new board, and this
answers it without guessing.

## Required hardware

Any RP2040 or RP2350 board, and something on an I2C bus. With nothing attached,
`scan` should report nothing — which is itself a useful check that the bus is
not stuck.

## Wiring

| Signal | Default pin |
|--------|-------------|
| SDA | GPIO 4 |
| SCL | GPIO 5 |

Both need pull-ups to 3V3. The `default` profile turns on the pads' internal
ones, which are ~50 kΩ — enough for a short lead at 100 kHz, and no substitute
for 4.7 kΩ resistors on a real board.

| Profile | Bus |
|---------|-----|
| `default` | i2c0 on GPIO 4/5, 100 kHz, internal pull-ups |
| `rp2040_zero` | SDK-default i2c1 on GPIO 6/7, 100 kHz, internal pull-ups |
| `bras_left` | i2c0 on GPIO 12/13, 400 kHz — the left sensor bus of `bras_attrape_caisse` |
| `bras_right` | i2c1 on GPIO 14/15, 400 kHz — its right sensor bus |

## Commands

```text
i2c> scan                  find every device on the bus
i2c> probe 68              does anything answer at 0x68
i2c> use 68                select it (add 'little' for byte order)
i2c> read 75               one byte
i2c> read 3B 2             two bytes, and the signed interpretation
i2c> write 6B 00           wake a device that boots asleep
i2c> dump 00 20            the first 32 registers
i2c> info                  bus configuration
```

Addresses and register numbers are hex; widths are decimal.

`scan` and `probe` each read one byte from the address and throw it away — the
hardware cannot send an address on its own, so there is no knock-only probe.
That byte is a real transaction: on a device with a read FIFO it consumes an
entry, and on one with a clear-on-read status register it clears it. Harmless
for finding out what is fitted on an idle bus, which is what this bench is for;
see the [component README](../../../components/i2c_device/README.md) before
pointing it at a device that is mid-conversion.

`read` prints both the unsigned and the signed interpretation, because which is
right depends on the register and only the datasheet knows.

## Interpreting failures

| Symptom | Likely cause |
|---------|--------------|
| `scan` finds nothing | no power to the device, SDA/SCL swapped, or no pull-ups |
| `scan` finds every address | SDA stuck low — a device holding the bus, or SDA shorted |
| `bus timeout` rather than `no device acknowledged` | the bus is stuck, not empty. Different problem: check pull-ups first |
| device answers `probe` but reads fail | it may use 16-bit register addresses; this bench's `read` uses 8-bit |
| values look plausible but wrong | byte order. Try `use <addr> little` |
| a signed reading is huge and positive | the field is narrower than the register; `read` shows the signed value for the full width, which is not the same thing |
| works at 100 kHz, fails at 400 | pull-ups too weak, or the lead too long |

## What this cannot tell you

Whether a device is configured correctly. Most sensors boot asleep and return
plausible-looking zeroes until a control register is written.
