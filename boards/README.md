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

No custom boards are defined yet; builds so far use the SDK's `pico`, `pico2`
and `pico2_w`.
