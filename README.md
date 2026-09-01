# pico-framework

A reusable firmware base for RP2040 and RP2350 boards, built directly on the
Raspberry Pi Pico SDK.

The Pico SDK stays the hardware abstraction layer. This repository adds project
structure, a pinned SDK, a build-configuration model, and (over time) a library
of reusable components. See [DESIGN_DOC.md](DESIGN_DOC.md) for the full design.

## Status

All 15 initial implementation priorities in DESIGN_DOC.md section 24 are
complete: the repository and build model, reusable components, host and target
tests, real application profiles, WiFi support, and the extension guide. The
framework now has 18 registered components; their individual READMEs distinguish
host coverage, build coverage, and physical hardware validation.

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
| [`firmware_update`](components/firmware_update/) | staged serial image reception, verification, and opt-in in-place install |
| [`flash_storage`](components/flash_storage/) | bounded erase/program/read, and the flash layout |
| [`i2c_device`](components/i2c_device/) | register access over I2C, with explicit byte order |
| [`vl53l0x`](components/vl53l0x/) | ST VL53L0X time-of-flight distance sensor |
| [`persistent_config`](components/persistent_config/) | key/value settings that survive power-off |
| [`logging`](components/logging/) | levelled logging, compile-time filtered, several sinks |
| [`wifi`](components/wifi/) | CYW43 station-mode connection management, with reconnect |
| [`bluetooth`](components/bluetooth/) | a serial console over Classic Bluetooth SPP |
| [`can`](components/can/) | CAN 2.0B over one PIO block with can2040 |

Updating a board over a serial link, with no USB involved, is built from these:
`hex_parser` decodes the image, `firmware_update` receives and verifies it,
`flash_storage` stages it, and `cli` carries both the records and the commands
on one console. [`serial_update_test`](apps/tests/serial_update_test/) is the
worked example.

Pure logic is covered by 18 sanitizer-enabled host executables. Hardware-facing
groups have manual applications under `apps/tests/`; a successful cross-build
is recorded separately from a physical result. The RP2040-Zero has validated
USB stdio and CLI, its onboard WS2812, bare-pin PIO UART loopback, persistent
configuration on flash, and an empty I2C bus. A Pico 2 W has validated both
radio components: `wifi` (association, address acquisition, RSSI, and
reconnection both from a console `connect` and from stored credentials after
a power cycle — see [`wifi`'s README](components/wifi/README.md) for a real
console-unresponsiveness caveat found during that validation) and
`bluetooth` (pairing with no PIN prompt, the SDP serial-port record, and
RFCOMM flow control under a deliberate flood — that validation found and
fixed a real bug in the test application itself, not the component; see
[`bt_console_test`'s README](apps/tests/bt_console_test/README.md)). CAN,
real I2C devices, and smart servos still await the hardware listed by their
test applications. The serial firmware installer has completed an end-to-end
stage, verify, in-place install, automatic reboot, and second transfer over a
hardware UART on the RP2040-Zero.

The serial updater is implemented as an application service: it stages and
verifies an image, then an opt-in RAM-resident routine installs it in place.
There is no separate resident bootloader. Its host logic and linked layout are
tested, and the end-to-end flash install has been validated on RP2040 hardware.

Still to come as components: TCP and UDP over the WiFi link, PWM, and a
flash-backed logging sink.

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
make BOARD=rp2040_zero                        # Waveshare RP2040-Zero
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
make BOARD=rp2040_zero APP=tests/ws2812_test PROFILE=rp2040_zero flash
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
lib/can2040/            PIO CAN controller, pinned submodule
components/             reusable libraries listed in the status table above
apps/minimal/           the smallest complete application
apps/tests/             hardware benches, often exercising several components
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

The contributor guide, [Extending pico-framework](docs/extending-the-framework.md),
walks through creating a component, application, profile, and custom board,
including their CMake conventions and testing checklist.

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

`make flash-serial` is a different path: it sends Intel HEX records through a
running firmware's CLI, with no USB or BOOTSEL dependency. The resident image
must include the firmware update service; `serial_update_test` is the reference
application.

```bash
make BOARD=pico2 APP=tests/serial_update_test PROFILE=rebuilt \
    flash-serial PORT=/dev/ttyACM0
```

The port may also be a bare make goal (`make ... flash-serial /dev/ttyACM0`).
With no port given, the script selects the only serial port or lists the choices.
It stages and verifies by default; add `APPLY=1` to invoke the deliberately
opt-in installer. `SERIAL_UPDATE_BAUD` selects the line rate for a real UART
(USB CDC ignores it). See
[`serial_update_test`](apps/tests/serial_update_test/README.md) for the safe
recovery assumptions and complete procedure.

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
| can2040             | `2988d4f` (submodule)                 |
| CMake               | 4.4.2 (minimum required: 3.13)        |
| `arm-none-eabi-gcc` | 16.2.0                                |
| Ninja               | used when present; Make otherwise     |
| picotool            | 2.3.0 (for `make flash`)              |
| Python 3            | 3.14 (required by the SDK build)      |

The top-level submodules provide the Pico SDK and can2040. Within the SDK, only
TinyUSB is initialized by default; `git submodule update --init --recursive`
fetches the rest (cyw43, lwIP, mbedTLS, BTstack) needed for wireless boards.

## Verified builds

Every application builds warning-free with `-Werror` for:

| BOARD      | Platform       | Architecture     |
|------------|----------------|------------------|
| `pico`     | rp2040         | Armv6-M          |
| `rp2040_zero` | rp2040      | Armv6-M — Waveshare RP2040-Zero |
| `pico2`    | rp2350-arm-s   | Armv8-M mainline |
| `pico2_w`  | rp2350-arm-s   | Armv8-M mainline |
| `bras_attrape_caisse` | rp2040 | Armv6-M — a custom header in `boards/` |

## Next steps

The initial roadmap in DESIGN_DOC.md section 24 is complete. Further work is
driven by actual project needs rather than another architecture phase:

- validate CAN, smart servos, and VL53L0X on the required hardware;
- decide how to handle the `wifi` component's console-blocking caveat
  (components/wifi/README.md) — confirmed by an A/B test to be a Pico SDK
  2.3.0 RP2350 regression (upstream issues #3078 and #3148, tracked for
  2.3.1), not a design flaw here; document-only for now, versus pinning the
  whole project to SDK 2.2.0 or a narrower `CYW43_CONFIG_FILE`-based
  workaround that keeps 2.3.0;
- add TCP/UDP, PWM, and persistent flash logging when an application needs them;
- exercise the framework in a complete robot application rather than only the
  minimal image and component benches.

The components ported so far come from the `Carte_actionneurs` Eurobot
firmware, rewritten to the framework's conventions: configuration structures
instead of `#define` blocks, caller-owned buffers, and no globals. Where that
firmware had the same code twice — the AX-12 and Feetech packet builders were
identical line for line — it is now shared through `servo_bus`.
