# pico-framework

A reusable firmware base for RP2040 and RP2350 boards, built directly on the
Raspberry Pi Pico SDK.

The Pico SDK stays the hardware abstraction layer. This repository adds project
structure, a pinned SDK, a build-configuration model, and (over time) a library
of reusable components. See [DESIGN_DOC.md](DESIGN_DOC.md) for the full design.

## Status

Implementation steps 1-5 of DESIGN_DOC.md section 24 are complete: repository
structure, pinned SDK submodule, the `BOARD` / `APP` / `PROFILE` CMake model,
the `minimal` application building for RP2040 and RP2350, and the Makefile
frontend.

No reusable components exist yet. `components/` is empty and
`cmake/components.cmake` holds an empty registration list.

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
├── pico/minimal/default/
├── pico2/minimal/default/
├── pico2/minimal/debug/
└── pico2_w/minimal/default/
```

Artifacts land in `build/$BOARD/$APP/$PROFILE/apps/$APP/app_$APP.{elf,uf2,bin,hex}`.

### Targets

| Target        | Effect                                                      |
|---------------|-------------------------------------------------------------|
| `build`       | configure if needed, then build (default target)            |
| `configure`   | configure the build directory only                          |
| `reconfigure` | delete and re-configure — needed after editing a profile     |
| `flash`       | build, then load over USB with `picotool`                    |
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
components/             reusable components (empty for now)
apps/minimal/           the smallest complete application
boards/                 custom Pico SDK board headers
profiles/minimal/       initial-cache profiles per application
config/                 non-secret application defaults that vary by deployment
```

`boards/` is on `PICO_BOARD_HEADER_DIRS`, so `BOARD=` accepts either an SDK
board or a custom header placed there.

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

The `minimal` application currently builds for:

| BOARD      | Platform       | Architecture     |
|------------|----------------|------------------|
| `pico`     | rp2040         | Armv6-M          |
| `pico2`    | rp2350-arm-s   | Armv8-M mainline |
| `pico2_w`  | rp2350-arm-s   | Armv8-M mainline |

## Next steps

Implementation continues with DESIGN_DOC.md section 24 step 6 onward: the
component target convention, the host-test harness and CI matrix, then porting
WS2812, the CLI, half-duplex UART, AX12, Feetech, and WiFi.
