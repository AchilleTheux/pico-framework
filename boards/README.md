# Boards

Custom Pico SDK board headers. This directory is appended to
`PICO_BOARD_HEADER_DIRS`, so `BOARD=<name>` resolves either an SDK board or a
`<name>.h` placed here.

A board header records fixed physical facts about a PCB — pin mappings, UART
and I2C/SPI instances, LED and button pins, the RP2040/RP2350 variant — and
nothing that varies by deployment. Settings that vary by deployment rather than
by PCB belong in `config/`; secrets belong in neither (see the README).

Custom headers usually start from an SDK board header in
`lib/pico-sdk/src/boards/include/boards/`.

## Project board names

| `BOARD` | Hardware | Definition |
|---------|----------|------------|
| `rp2040_zero` | Waveshare RP2040-Zero, 2 MiB flash, onboard WS2812 on GPIO16 | thin alias of the Pico SDK's `waveshare_rp2040_zero` |
| `bras_attrape_caisse` | Eurobot 2026 actuator board | project schematic-derived header |

The alias deliberately includes the SDK header rather than copying it. SDK
updates therefore remain the single source of truth for the RP2040-Zero's
default UART (GPIO0/1), I2C (GPIO6/7), SPI (GPIO10..13), flash configuration,
and onboard WS2812 pin.

## Hardware validation

The `rp2040_zero` target was exercised on a Waveshare RP2040-Zero over USB CDC
on 2026-08-31:

- minimal firmware identity and periodic output;
- interactive CLI parsing and error paths;
- onboard GPIO16 WS2812 colour order and the timed blue/red/green/off loop,
  visually confirmed;
- bare-pin PIO half-duplex UART loopback at 57,600 through 1,000,000 baud
  (`11 checks, 0 failed`);
- persistent configuration save, alternating flash slots, and reload after a
  reboot/reflash;
- SDK-default i2c1 on GPIO6/7, with an empty-bus scan confirming that the bus
  was not stuck.

CAN and servo tests still require their transceivers and peers; I2C needs a real
device before register transfers are validated. Radio testing requires a Pico W
or Pico 2 W, and the update installer remains deliberately unrun pending a
review and BOOTSEL recovery setup.
