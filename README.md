# pico-framework

A reusable firmware base for RP2040 and RP2350 boards, built directly on the
Raspberry Pi Pico SDK.

The Pico SDK stays the hardware abstraction layer. This repository adds project
structure, a pinned SDK, a build-configuration model, and (over time) a library
of reusable components. See [DESIGN_DOC.md](DESIGN_DOC.md) for the full design.

## Status

Implementation steps 1-12 of DESIGN_DOC.md section 24 are complete: repository
structure, pinned SDK submodule, the `BOARD` / `APP` / `PROFILE` CMake model,
the `minimal` application building for RP2040 and RP2350, the Makefile
frontend, the component target convention, the host-test harness, and the
first two components.

| Component | What it provides |
|-----------|------------------|
| [`ws2812`](components/ws2812/) | WS2812 / SK6812 LED strips, RGB and RGBW |
| [`cli`](components/cli/) | line-oriented command interpreter, transport-agnostic |
| [`half_duplex_uart`](components/half_duplex_uart/) | 8N1 over one shared wire, for smart-servo buses |
| [`servo_bus`](components/servo_bus/) | Dynamixel Protocol 1.0 packets, transactions, retries |
| [`ax12`](components/ax12/) | Dynamixel AX-12 / AX-18 control table and operations |
| [`feetech`](components/feetech/) | Feetech STS / SMS / SCS control table and operations |
| [`crc`](components/crc/) | CRC-32 and CRC-16, standard parameterisations |
| [`ring_buffer`](components/ring_buffer/) | byte FIFO over caller-owned storage |
| [`hex_parser`](components/hex_parser/) | Intel HEX record decoding |
| [`firmware_update`](components/firmware_update/) | image header and the boot decision (pure half) |
| [`flash_storage`](components/flash_storage/) | bounded erase/program/read, and the flash layout |

Each has host tests and a hardware test application. `ws2812` and `cli` are
ported from working firmware and exercise their hardware paths; the four servo
components build clean for both architectures and their protocol handling is
checked against the AX-12 datasheet by the host tests, but **none of them has
yet been run against a real servo bus**.

The last four are the foundations of updating firmware over a serial link,
without needing the BOOTSEL button. Their pure logic is complete and tested;
the flash-backed half and the bootloader itself are not written yet.

Still to come: the flash and bootloader half of the update path, PWM, I2C, and
WiFi.

## Getting started

```bash
git clone --recursive <repository> my-project
cd my-project
make
```

If the repository was cloned without `--recursive`:

```bash
git submodule update --init --recursive
```

The build uses the SDK in `lib/pico-sdk`; no global SDK installation or
`PICO_SDK_PATH` environment variable is needed.

## Building

A build is identified by three variables:

| Variable  | Meaning                            | Maps to                |
|-----------|------------------------------------|------------------------|
| `BOARD`   | hardware                           | `PICO_BOARD`           |
| `APP`     | the firmware program under `apps/`  | `PICO_FRAMEWORK_APP`   |
| `PROFILE` | settings for that application      | `-C profiles/$APP/$PROFILE.cmake` |

Defaults are `BOARD=pico APP=minimal PROFILE=default`.

```bash
make                                          # pico / minimal / default
make BOARD=pico2                              # RP2350
make BOARD=pico2_w                            # RP2350 with WiFi
make BOARD=pico2 PROFILE=debug
make BOARD=pico2 PROFILE=debug flash
```

Make variables do not persist between invocations, so **every** command for a
non-default configuration must repeat all three. A bare `make flash` flashes
the default configuration.

Each combination gets its own build directory, so configurations coexist:

```text
build/
├── host-tests/
├── pico/minimal/default/
├── pico2/minimal/default/
├── pico2/minimal/debug/
├── pico2/tests/cli_test/default/
├── pico2/tests/half_duplex_uart_test/default/
├── pico2/tests/servo_test/ax12/
├── pico2/tests/ws2812_test/default/
└── pico2_w/minimal/default/
```

`APP` may name a nested directory, so hardware tests live under `apps/tests/`:

```bash
make BOARD=pico2 APP=tests/ws2812_test flash
```

Artifacts land in `build/$BOARD/$APP/$PROFILE/apps/$APP/app_$APP.{elf,uf2,bin,hex}`.

### Targets

| Target        | Effect                                                      |
|---------------|-------------------------------------------------------------|
| `build`       | configure if needed, then build (default target)            |
| `configure`   | configure the build directory only                          |
| `reconfigure` | delete and re-configure — needed after editing a profile     |
| `flash`       | build, then load over USB with `picotool`                    |
| `flash-serial`| reboot the board on a named serial port, then load           |
| `test`        | build and run the host-side unit tests                      |
| `ci`          | everything CI checks: tests plus the whole build matrix      |
| `size`        | build, then report section sizes                            |
| `clean`       | remove the selected configuration's build directory          |
| `distclean`   | remove `build/` entirely                                     |
| `apps`        | list available applications                                  |
| `profiles`    | list profiles for `APP`                                      |
| `help`        | usage and the current configuration                          |

`make help` works in a fresh clone, before the submodules are initialized.

CMake remains the real build system. The Makefile only selects a build
directory and forwards to CMake; it contains no dependency logic of its own.
Building with CMake directly is equivalent:

```bash
cmake -S . -B build/pico2/minimal/debug -G Ninja \
    -C profiles/minimal/debug.cmake \
    -DPICO_BOARD=pico2 \
    -DPICO_FRAMEWORK_APP=minimal \
    -DPICO_FRAMEWORK_PROFILE=debug
cmake --build build/pico2/minimal/debug
```

## Profiles

A profile is an initial CMake cache holding frequently used settings for one
application. It does not select the board or the application, and it does not
list component dependencies.

Because an initial cache is only applied when a build directory is first
configured, editing a profile has no effect on an existing build directory.
Use `make reconfigure` (or `make clean` then `make`) after editing one.

## Repository layout

```text
CMakeLists.txt          top level: project(), SDK init, application selection
Makefile                developer frontend to CMake
cmake/
  project_options.cmake framework-wide settings, BOARD/APP/PROFILE plumbing
  components.cmake      explicit registration list for reusable components
lib/pico-sdk/           Pico SDK, pinned submodule
components/ws2812/      addressable LED strips
components/cli/         command interpreter
components/half_duplex_uart/  single-wire UART for servo buses
components/servo_bus/   Protocol 1.0 packets and transactions
components/ax12/        Dynamixel AX-12 servos
components/feetech/     Feetech STS/SMS/SCS servos
components/crc/         CRC-32 and CRC-16
components/ring_buffer/ byte FIFO
components/hex_parser/  Intel HEX decoding
components/firmware_update/  firmware image format and boot decision
components/flash_storage/    bounded flash access and the chip layout
apps/minimal/           the smallest complete application
apps/tests/             one hardware test application per component
boards/                 custom Pico SDK board headers
profiles/<app>/         initial-cache profiles per application
config/                 non-secret application defaults that vary by deployment
tests/                  host-side unit tests (separate CMake project)
```

`boards/` is on `PICO_BOARD_HEADER_DIRS`, so `BOARD=` accepts either an SDK
board or a custom header placed there.

## Testing

Two kinds, matching DESIGN_DOC.md section 19.

**Host tests** cover logic that calls no Pico SDK function — packet encoding,
checksums, colour maths, command parsing. They build with the host compiler as
a separate CMake project under `tests/`, with warnings as errors and ASan and
UBSan on:

```bash
make test
```

**Hardware tests** are applications under `apps/tests/`, one per component,
each with a README giving the required board, wiring, procedure, and expected
output. They are run manually:

```bash
make BOARD=pico2 APP=tests/ws2812_test flash
```

Adding a component means adding both: the pure part goes in a file the host
tests can compile directly, and the hardware part gets a test application. The
`cli` component is the clearest example — because it reaches the world only
through two function pointers, the whole interpreter is exercised on the host
against a fake stream.

Where a component implements someone else's specification, the tests are
written against that specification rather than against the implementation:
`servo_protocol_test.c` checks the packet builder byte for byte against the
worked examples in the AX-12 datasheet, so a consistent misreading of the
format cannot pass.

### Flashing without the BOOTSEL button

`make flash` already avoids the button. Every application here is built with
`pico_enable_stdio_usb`, and the SDK then enables two reset paths by default: a
vendor USB interface, which is what `picotool load -f` uses to reset a running
board, and a 1200-baud touch on the CDC port.

`make flash-serial` addresses the case `picotool` alone handles badly — several
boards plugged in at once:

```bash
make flash-serial /dev/ttyACM0
make BOARD=pico2 APP=tests/servo_test PROFILE=ax12 flash-serial PORT=/dev/ttyACM1
```

Both spellings work. With no port given it picks the only one present, and
lists them when there is a choice.

It asks the firmware on that port to reboot, two ways, since which one applies
depends on how the board is attached:

| Mechanism | Works with |
|---|---|
| the CLI command `bootsel` | firmware with the `cli` component and a command calling `reset_usb_boot()` — including over a real UART |
| a 1200-baud touch | any firmware built with `pico_enable_stdio_usb`; USB CDC only |

Then it waits for the board in BOOTSEL and loads with `picotool`.

**The upload is still over USB.** What naming a port buys is knowing *which*
board gets flashed: rebooting through one specific port puts exactly one board
into BOOTSEL, where `picotool load -f` on a busy bench picks one for you. A
genuine serial-only upload needs the resident bootloader that is not built yet;
when it exists it belongs in the same script, tried before the USB path.

Overridable through the environment: `SERIAL_RESET_COMMAND`, `SERIAL_RESET_BAUD`,
`SERIAL_RESET_TIMEOUT`.

### CI

```bash
make ci
```

runs the host tests and builds every board / application / profile combination
with warnings as errors, reporting flash and RAM per configuration and the
toolchain and SDK versions that produced them.

The matrix lives in [`scripts/ci.sh`](scripts/ci.sh), not in the workflow file,
so it has one definition and a failure can be reproduced locally without
pushing. `.github/workflows/ci.yml` just calls it. `scripts/ci.sh --quick`
trims the matrix to one board per architecture.

The workflow has not run yet — this repository has no remote.

### Warnings

Framework and application sources build with `-Wall -Wextra -Wshadow -Wundef`
via `pico_framework_set_warnings()`. Configure with
`-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON` to make them fatal, as CI should:

```bash
make CMAKE_ARGS=-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON
```

The flags are set per source file rather than per target on purpose. Most Pico
SDK libraries are INTERFACE targets carrying sources, so linking `pico_stdlib`
compiles SDK `.c` files into your target; a target-level flag would land on SDK
code too, and `-Wundef` fails to build it.

## Secrets

Do not commit credentials in board, profile, or config files. `config/local.cmake`
is gitignored and is the intended place for local, uncommitted settings.

## Toolchain

Reproducible builds depend on more than the pinned SDK. This tree is built and
verified with:

| Tool                | Version pinned / verified            |
|---------------------|--------------------------------------|
| Pico SDK            | `2.3.0` (submodule, detached tag)     |
| CMake               | 4.4.2 (minimum required: 3.13)        |
| `arm-none-eabi-gcc` | 16.2.0                                |
| Ninja               | used when present; Make otherwise     |
| picotool            | 2.3.0 (for `make flash`)              |
| Python 3            | 3.14 (required by the SDK build)      |

Only `lib/pico-sdk/lib/tinyusb` is initialized by default beyond the SDK
itself; `git submodule update --init --recursive` fetches the rest (cyw43,
lwIP, mbedTLS, BTstack) needed for wireless boards.

## Verified builds

Every application builds warning-free with `-Werror` for:

| BOARD      | Platform       | Architecture     |
|------------|----------------|------------------|
| `pico`     | rp2040         | Armv6-M          |
| `pico2`    | rp2350-arm-s   | Armv8-M mainline |
| `pico2_w`  | rp2350-arm-s   | Armv8-M mainline |
| `bras_attrape_caisse` | rp2040 | Armv6-M — a custom header in `boards/` |

## Next steps

Implementation continues with DESIGN_DOC.md section 24: profiles justified by
real configurations, then WiFi, then the documentation on creating a component,
application, profile and custom board.

The components ported so far come from the `Carte_actionneurs` Eurobot
firmware, rewritten to the framework's conventions: configuration structures
instead of `#define` blocks, caller-owned buffers, and no globals. Where that
firmware had the same code twice — the AX-12 and Feetech packet builders were
identical line for line — it is now shared through `servo_bus`.
