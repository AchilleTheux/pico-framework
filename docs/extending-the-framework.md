# Extending pico-framework

This guide covers the four extension points used by the framework: components,
applications, profiles, and boards. They have deliberately different jobs:

| Extension | Owns | Does not own |
|-----------|------|--------------|
| Component | reusable behaviour and its hardware-independent policy | project pin assignments or deployment settings |
| Application | the firmware entry point, peripheral ownership, and component composition | facts fixed by the PCB |
| Profile | settings for one application in one deployment or bench setup | board or application selection, secrets, dependencies |
| Board | fixed physical facts about a PCB | settings that vary between deployments |

A build selects one of each relevant kind with `BOARD`, `APP`, and `PROFILE`.
Components are normal CMake dependencies selected by the application, never a
fourth build variable.

## Create a component

Use this layout:

```text
components/example/
├── CMakeLists.txt
├── README.md
├── example.c
└── include/
    └── example.h
```

Define a namespaced library target in `components/example/CMakeLists.txt`:

```cmake
add_library(pico_framework_example STATIC
    example.c
)

add_library(pico_framework::example ALIAS pico_framework_example)

target_include_directories(pico_framework_example PUBLIC
    include
)

pico_framework_set_warnings(example.c)

target_link_libraries(pico_framework_example PUBLIC
    pico_stdlib
)
```

Use `PUBLIC` for dependencies exposed by the component's public headers and
`PRIVATE` for implementation-only dependencies. If the component depends on
another framework component, link its alias:

```cmake
target_link_libraries(pico_framework_example PUBLIC
    pico_framework::ring_buffer
)
```

Register the directory in `PICO_FRAMEWORK_COMPONENTS` in
[`cmake/components.cmake`](../cmake/components.cmake). Registration does not
put the component in every image: component directories are added with
`EXCLUDE_FROM_ALL`, so a component builds only when the selected application
can reach it through `target_link_libraries()`.

### Design the API

- Pass pins, peripheral instances, timing, and buffers through an explicit
  configuration structure.
- Let the caller own storage. Do not hide allocation or retain pointers unless
  the ownership contract says so.
- Return errors instead of waiting forever or silently accepting invalid
  settings.
- State which PIO blocks, state machines, IRQs, DMA channels, pins, or cores the
  component owns and when it releases them.
- Keep SDK-independent calculations and formats in separate source files when
  possible. Host tests can compile those files directly.
- Put usage, resource ownership, limitations, and testing status in the
  component README.

Existing examples illustrate different shapes:

- [`ring_buffer`](../components/ring_buffer/) is pure C with no SDK dependency.
- [`ws2812`](../components/ws2812/) separates colour maths from PIO and DMA.
- [`ax12`](../components/ax12/) exposes another component as a transitive
  dependency.
- [`can`](../components/can/) wraps an external library and documents strict
  PIO and IRQ ownership.

### Add tests

Pure logic belongs in `tests/components/`. Register its executable with the
`host_test()` helper in [`tests/CMakeLists.txt`](../tests/CMakeLists.txt):

```cmake
host_test(example_test
    SOURCES
        tests/components/example_test.c
        components/example/example_logic.c
)
target_include_directories(example_test PRIVATE
    "${PICO_FRAMEWORK_ROOT}/components/example/include"
)
```

Hardware behaviour belongs in an application under `apps/tests/example_test/`.
Its README must give the required equipment, wiring, commands, expected result,
failure interpretation, and the limits of what the test proves. A component is
not hardware-validated merely because its application builds.

Run both layers before committing:

```bash
make test
make BOARD=pico APP=tests/example_test PROFILE=default \
    CMAKE_ARGS=-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON
```

Add representative board/application/profile combinations to the matrix in
[`scripts/ci.sh`](../scripts/ci.sh). The matrix should cover meaningful code
paths, not every possible Cartesian product.

## Create an application

An application is one complete firmware image. It owns `main()`, chooses and
configures peripherals, provides component buffers, and calls component APIs.

Use this layout for `APP=my_app`:

```text
apps/my_app/
├── CMakeLists.txt
└── main.c

profiles/my_app/
└── default.cmake
```

The target must be named `app_<last directory name>`. This keeps artifact paths
predictable even for nested applications such as `APP=tests/can_test`:

```cmake
add_executable(app_my_app
    main.c
)

set(MY_APP_BUS_SPEED 500000 CACHE STRING "Bus speed in bit/s")

target_compile_definitions(app_my_app PRIVATE
    MY_APP_BUS_SPEED=${MY_APP_BUS_SPEED}
)

target_link_libraries(app_my_app PRIVATE
    pico_stdlib
    pico_framework::example
)

pico_enable_stdio_usb(app_my_app 1)
pico_enable_stdio_uart(app_my_app 1)

pico_framework_set_warnings(main.c)
pico_add_extra_outputs(app_my_app)
```

Only application source files should be passed to
`pico_framework_set_warnings()`. Pico SDK interface targets compile some SDK
sources into the application, and the framework intentionally does not impose
its warning policy on pinned third-party code.

No central application registry is needed. The top-level build accepts an
application as soon as `apps/<APP>/CMakeLists.txt` exists. Check discovery and
build it with:

```bash
make apps
make BOARD=pico APP=my_app PROFILE=default
make BOARD=pico APP=my_app PROFILE=default flash
```

Repeat all three variables for every non-default command. Make variables do not
persist between invocations, and a bare `make flash` means
`BOARD=pico APP=minimal PROFILE=default`.

## Create a profile

A profile is a CMake initial-cache file under
`profiles/<application>/<name>.cmake`. It gives a real deployment or bench
configuration a reviewable name:

```cmake
# 500 kbit/s bench bus using a transceiver on GPIO4/5.
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(MY_APP_BUS_SPEED 500000 CACHE STRING "Bus speed in bit/s")
set(MY_APP_RX_PIN    4      CACHE STRING "Transceiver RXD pin")
set(MY_APP_TX_PIN    5      CACHE STRING "Transceiver TXD pin")
set(MY_APP_VERBOSE   ON     CACHE BOOL   "Enable verbose diagnostics")
```

The matching application declares the same cache variables and translates
them into compile definitions or other target properties. Prefer typed cache
entries (`BOOL`, `STRING`, `PATH`) and comments explaining the physical setup.

A profile may set application options and `CMAKE_BUILD_TYPE`. It must not:

- select `PICO_BOARD` or `PICO_FRAMEWORK_APP`;
- list component dependencies;
- contain Wi-Fi passwords, tokens, keys, or other secrets;
- duplicate fixed board facts merely to avoid using the board header.

Profiles are loaded with `cmake -C` only when a build directory is first
configured. After editing a profile, recreate that configuration so the new
values take effect:

```bash
make BOARD=pico APP=my_app PROFILE=bench reconfigure
make BOARD=pico APP=my_app PROFILE=bench
```

This is intentionally scoped: `reconfigure` removes only that
board/application/profile build directory. Other configurations remain intact.

List the profiles for an application with:

```bash
make APP=my_app profiles
```

## Create a custom board

First check `lib/pico-sdk/src/boards/include/boards/`. If the SDK already
defines the hardware, use its board name directly or create a thin project
alias rather than copying its flash and pin definitions. The
[`rp2040_zero`](../boards/rp2040_zero.h) header is such an alias.

For a genuinely custom PCB, add `boards/<name>.h`. Board headers are also read
by the assembler preprocessor, so keep them to preprocessor directives and
Pico SDK board CMake macros—no C declarations.

```c
#ifndef _BOARDS_MY_BOARD_H
#define _BOARDS_MY_BOARD_H

pico_board_cmake_set(PICO_PLATFORM, rp2040)

#define MY_BOARD 1

#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1
pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

#endif
```

Record only fixed PCB facts:

- RP2040/RP2350 platform and flash configuration;
- default UART, I2C, SPI, LED, and button definitions understood by the SDK;
- named project pins for permanently wired peripherals.

Deployment-dependent bitrates, logging levels, feature switches, and similar
settings belong in a profile. Secrets belong in neither place; provision them
at runtime, normally through `persistent_config`.

Validate a new board with the smallest image first, then only the hardware that
is actually connected:

```bash
make BOARD=my_board APP=minimal PROFILE=default \
    CMAKE_ARGS=-DPICO_FRAMEWORK_WARNINGS_AS_ERRORS=ON
make BOARD=my_board APP=minimal PROFILE=default flash
```

Add at least the minimal build to `scripts/ci.sh`, document the board in
[`boards/README.md`](../boards/README.md), and record physical validation
separately from build validation.

## Before opening a change

Use the smallest relevant checks while developing, then run the repository
gate:

```bash
make test
make ci
git diff --check
```

`make ci` runs sanitizer-enabled host tests, builds the firmware matrix with
warnings as errors, and checks that the host and target implementations agree
on firmware-image checksums. Hardware results still need to be recorded from a
physical test; CI cannot infer them from a successful cross-compile.
