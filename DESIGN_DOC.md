# Pico Framework — Simple Design Document

## 1. Purpose

The goal of this project is to create a reusable firmware base for RP2040 and RP2350 microcontrollers using the Raspberry Pi Pico SDK.

The framework should make it easy to start new embedded projects without repeatedly copying drivers, utility code, CMake configuration, and initialization code from older projects.

It should provide:

* A standard project structure around the Pico SDK.
* Pico SDK included as a pinned Git submodule.
* Support for both RP2040 and RP2350.
* Reusable firmware components.
* CMake-based dependency and feature management.
* A simple Makefile frontend for common build operations.
* Configurable firmware profiles.
* Support for multiple boards.
* A clean separation between reusable code and project-specific application logic.

The framework should remain lightweight and close to the Pico SDK. It should not attempt to replace or hide the Pico SDK behind a large abstraction layer.

---

## 2. Main Design Principles

### 2.1 Pico SDK remains the hardware abstraction layer

The project should directly use Pico SDK APIs for hardware access.

For example:

```c
gpio_init();
uart_init();
pio_add_program();
dma_channel_configure();
```

The framework should not create unnecessary wrappers around APIs already provided cleanly by the Pico SDK.

Reusable components should only be created where they provide actual additional behavior or reusable logic.

For example:

Good reusable components:

* WS2812 driver
* AX12 protocol
* Feetech protocol
* CLI
* half-duplex UART transport
* button debouncing
* persistent configuration
* WiFi manager
* logging

Probably unnecessary:

* generic GPIO wrapper around `gpio_put()`
* generic delay wrapper around `sleep_ms()`

---

## 3. Git Strategy

Git branches must not be used to select firmware features.

A feature such as AX12, WiFi, WS2812, or CLI should exist as a module in the main source tree.

Feature branches should only be temporary development branches.

Example:

```text
main
├── feature/ax12-sync-write
├── feature/wifi-reconnect
├── feature/cli-history
└── fix/ws2812-timing
```

When a feature is stable, it is merged into `main`.

The `main` branch should contain all stable reusable components.

Firmware composition is handled by CMake rather than by merging Git branches.

---

## 4. Repository Structure

Initial proposed structure:

```text
pico-framework/
├── CMakeLists.txt
├── Makefile
├── README.md
├── .gitmodules
│
├── lib/
│   └── pico-sdk/
│
├── components/
│   ├── ws2812/
│   ├── cli/
│   ├── serial/
│   ├── half_duplex_uart/
│   ├── ax12/
│   ├── feetech/
│   ├── wifi/
│   └── ...
│
├── apps/
│   ├── minimal/
│   ├── examples/
│   └── tests/
│
├── boards/
│   └── ...
│
├── profiles/
│   ├── minimal/
│   │   └── default.cmake
│   └── servo_controller/
│       ├── default.cmake
│       └── debug.cmake
│
├── config/
│   └── ...
│
└── cmake/
    ├── components.cmake
    ├── project_options.cmake
    └── ...
```

---

## 5. Pico SDK Integration

The Pico SDK should be included as a Git submodule:

```text
lib/pico-sdk/
```

The framework should pin the SDK to a known tag or commit.

This pins the main external firmware dependency and prevents SDK updates from unexpectedly breaking existing firmware. Fully repeatable builds also depend on the compiler, CMake, and related tools, so supported tool versions should be recorded in the README and exercised in CI.

A project should be clonable with:

```bash
git clone --recursive <repository>
```

or initialized afterwards with:

```bash
git submodule update --init --recursive
```

The build system should use the local SDK rather than requiring the user to configure a global SDK installation.

---

## 6. Component System

Reusable functionality should live in:

```text
components/
```

Each component should normally contain:

```text
component_name/
├── CMakeLists.txt
├── include/
│   └── component_name.h
└── component_name.c
```

Additional files such as PIO programs may also be included.

Example:

```text
components/ws2812/
├── CMakeLists.txt
├── include/
│   └── ws2812.h
├── ws2812.c
└── ws2812.pio
```

Each component should be exposed as a CMake library target with a project-specific name and a namespaced alias.

Example:

```cmake
add_library(pico_framework_ws2812 STATIC
    ws2812.c
)

add_library(pico_framework::ws2812 ALIAS pico_framework_ws2812)

target_include_directories(pico_framework_ws2812 PUBLIC
    include
)

target_link_libraries(pico_framework_ws2812 PUBLIC
    pico_stdlib
    hardware_pio
)
```

Applications should depend on components through CMake targets rather than manually adding their source files.

---

## 7. Component Dependencies

Components may depend on other components.

CMake should resolve these relationships naturally.

Example:

```text
AX12
 └── half_duplex_uart
      └── Pico SDK UART

Feetech
 └── half_duplex_uart
      └── Pico SDK UART
```

The corresponding CMake relationship may be:

```cmake
target_link_libraries(pico_framework_ax12 PUBLIC
    pico_framework::half_duplex_uart
)
```

and:

```cmake
target_link_libraries(pico_framework_feetech PUBLIC
    pico_framework::half_duplex_uart
)
```

This avoids duplicated transport code. Applications and profiles should not have to list transitive dependencies such as `half_duplex_uart`; component targets declare them.

---

## 8. Transport Abstractions

Where useful, protocol implementations should be separated from their transport.

For example, the command-line interpreter should preferably not depend directly on `uart0`.

Instead:

```text
CLI
 │
 └── stream interface
      ├── UART
      ├── USB CDC
      └── TCP
```

Similarly:

```text
AX12
 │
 └── half-duplex byte transport
```

This allows the same higher-level component to be reused with different physical interfaces.

These abstractions should remain small and practical.

The framework should avoid introducing generic abstraction layers without a concrete need.

The first transport APIs should be synchronous and should define timeout behavior, error reporting, buffer ownership, and peripheral ownership. Asynchronous or shared-peripheral support should only be added when an application needs it.

---

## 9. Applications

Reusable components should not contain application-specific behavior.

Project or firmware logic should live in:

```text
apps/
```

An application represents a complete firmware program.

Exactly one application is selected for each build with Makefile variable `APP`, which maps to CMake variable `PICO_FRAMEWORK_APP`. The initial default application is `minimal`.

Example:

```text
apps/servo_controller/
├── CMakeLists.txt
├── main.c
├── servo_controller.c
└── servo_controller.h
```

Possible applications during development include:

```text
minimal
servo_test
wifi_test
ax12_test
feetech_test
cli_test
```

This also provides an easy way to create hardware test firmware.

For example:

```text
ax12_test
├── AX12
├── CLI
└── WS2812
```

can be compiled independently from a larger robot application.

An application target explicitly links its direct component dependencies:

```cmake
target_link_libraries(app_servo_controller PRIVATE
    pico_framework::ax12
    pico_framework::ws2812
)
```

---

## 10. Build Configuration Model

Every build is identified by three inputs:

* `BOARD` selects the hardware through the Pico SDK board mechanism.
* `APP` selects the complete firmware executable.
* `PROFILE` optionally selects settings for that application.

The Makefile maps `BOARD` to `PICO_BOARD`, maps `APP` to `PICO_FRAMEWORK_APP`, and loads `profiles/${APP}/${PROFILE}.cmake` as the initial CMake cache. `PROFILE=default` is used when no profile is specified.

Components are CMake dependencies, not a fourth user-facing selection mechanism. The top-level build should register component directories with `EXCLUDE_FROM_ALL`, then add only the selected application:

```cmake
add_subdirectory(components/half_duplex_uart EXCLUDE_FROM_ALL)
add_subdirectory(components/ax12 EXCLUDE_FROM_ALL)
add_subdirectory(components/ws2812 EXCLUDE_FROM_ALL)

set(PICO_FRAMEWORK_APP minimal CACHE STRING "Application to build")
add_subdirectory(apps/${PICO_FRAMEWORK_APP})
```

Only components linked by the selected application are built. Their transitive dependencies are resolved by `target_link_libraries()`.

Application-specific optional behavior may use a scoped CMake option. For example:

```cmake
option(SERVO_CONTROLLER_ENABLE_CLI "Enable the debug CLI" OFF)

if(SERVO_CONTROLLER_ENABLE_CLI)
    target_link_libraries(app_servo_controller PRIVATE
        pico_framework::cli
    )
endif()
```

Avoid global options such as `FEATURE_UART` for transitive implementation details. Keep registration explicit until repeated use demonstrates a need for automation.

---

## 11. Profiles

Frequently used settings for an application should be stored as profiles. Profiles do not select the application or board and do not list transitive component dependencies.

Profiles should live in:

```text
profiles/
```

Example:

```text
profiles/
├── minimal/
│   └── default.cmake
└── servo_controller/
    ├── default.cmake
    └── debug.cmake
```

Example `profiles/servo_controller/debug.cmake`:

```cmake
set(SERVO_CONTROLLER_ENABLE_CLI ON CACHE BOOL "Enable the debug CLI")
```

A profile represents a known configuration of one application, not a separate Git branch.

Typical build:

```bash
cmake -S . -B build/pico2/servo_controller/debug \
    -DPICO_BOARD=pico2 \
    -DPICO_FRAMEWORK_APP=servo_controller \
    -C profiles/servo_controller/debug.cmake
```

Initial-cache profiles are loaded when a build directory is first configured. Changing `BOARD`, `APP`, or `PROFILE` therefore selects a different build directory instead of reusing an incompatible CMake cache.

---

## 12. Board Selection

Board, application, and profile selection must remain independent.

For example:

```text
BOARD=pico2
APP=servo_controller
PROFILE=debug
```

means:

```text
Hardware:
    Pico 2 / RP2350

Firmware:
    servo_controller application
    AX12
    CLI
    WS2812
```

Another build could use:

```text
BOARD=custom_robot_board
APP=servo_controller
PROFILE=debug
```

without changing the application code.

Board selection should use the Pico SDK board mechanism. The Makefile variable `BOARD` maps directly to CMake's `PICO_BOARD`.

---

## 13. Hardware Configuration

Reusable drivers must not hard-code project-specific pins.

Avoid:

```c
#define AX12_UART uart1
#define AX12_TX_PIN 8
```

inside the AX12 driver.

Prefer configuration structures:

```c
ax12_config_t config = {
    .uart = uart1,
    .baudrate = 1000000,
    .tx_pin = 8,
    .rx_pin = 9,
    .direction_pin = 10,
};

ax12_init(&config);
```

The configuration layers have distinct responsibilities:

* `boards/` contains custom Pico SDK board headers and fixed physical facts such as pin mappings and available peripherals.
* `config/` contains optional, non-secret application defaults that vary by deployment rather than by PCB.
* The selected application reads those values and constructs component configuration structures.
* Components receive explicit configuration and do not search global board or application configuration.

Board definitions may provide things such as:

* UART instances
* GPIO pins
* I2C buses
* SPI buses
* LED pins
* button pins
* servo direction pins
* board-specific peripherals

This allows the same component to run on several boards.

There is no implicit override chain: one board is selected at configure time, and the application explicitly passes the resulting values to each component. Secrets such as WiFi credentials must not be committed in board or profile files; they should be supplied through a documented local configuration mechanism.

---

## 14. RP2040 and RP2350 Support

The framework should support both RP2040 and RP2350 from the beginning.

Most reusable components should be architecture-independent and rely on Pico SDK APIs.

Platform-specific code should only be used where required.

Avoid spreading constructs such as:

```c
#ifdef PICO_RP2040
...
#elif PICO_RP2350
...
#endif
```

through application code.

If platform-specific implementation is necessary, it should preferably be isolated inside the corresponding component.

Typical board usage:

```bash
make BOARD=pico APP=minimal PROFILE=default
```

for RP2040.

```bash
make BOARD=pico2 APP=minimal PROFILE=default
```

for RP2350.

```bash
make BOARD=pico2_w APP=minimal PROFILE=default
```

for RP2350 with WiFi.

---

## 15. Makefile

CMake should remain the real build system.

The Makefile should only provide convenient commands for developers.

It must not duplicate the dependency or build logic already implemented in CMake.

Typical interface:

```bash
make
make clean
make flash
```

with configurable variables:

```bash
make BOARD=pico2 APP=servo_controller PROFILE=debug
```

`make` may default to `BOARD=pico APP=minimal PROFILE=default`. Command-line variables do not persist between Make invocations, so every command for a non-default configuration must repeat all three values. In particular, `make flash` always means the default configuration unless variables are supplied explicitly.

The Makefile may internally generate separate build directories such as:

```text
build/
├── pico/
│   └── minimal/
│       └── default/
│
├── pico2/
│   ├── minimal/
│   │   └── default/
│   └── servo_controller/
│       ├── default/
│       └── debug/
│
└── pico2_w/
    └── wifi_test/
        └── default/
```

This allows several configurations to coexist without repeatedly reconfiguring the same build directory.

---

## 16. Typical Development Workflow

A new firmware project should eventually be creatable with a workflow similar to:

```bash
git clone --recursive <pico-framework> my-project
cd my-project
```

Select the hardware, application, and profile:

```bash
make BOARD=pico2 APP=servo_controller PROFILE=debug
```

Flash:

```bash
make BOARD=pico2 APP=servo_controller PROFILE=debug flash
```

Development loop:

```text
edit application code
        ↓
make BOARD=pico2 APP=servo_controller PROFILE=debug
        ↓
make BOARD=pico2 APP=servo_controller PROFILE=debug flash
        ↓
      test
```

Most work for a new project should involve application logic and configuration rather than recreating infrastructure.

---

## 17. Initial Reusable Components

The first components should come from code already commonly reused between existing projects.

Likely initial modules:

### WS2812

Responsibilities:

* PIO-based WS2812 transmission
* one or more LEDs
* RGB values
* optional brightness control
* simple status LED usage

### CLI / UART Interpreter

Responsibilities:

* receive text commands
* command registration
* argument parsing
* command callbacks
* optional help system

The parser should be independent from the transport from its first implementation.

### Half-Duplex UART

Responsibilities:

* UART configuration
* TX/RX direction management
* optional direction GPIO
* timing management
* shared transport API

Intended users:

* AX12
* Feetech
* other half-duplex protocols

### AX12

Responsibilities:

* packet encoding
* packet decoding
* checksum
* read/write operations
* ping
* register access
* servo commands

Transport should be provided separately.

### Feetech

Responsibilities:

* packet handling
* servo register access
* servo commands
* protocol-specific features

Transport should reuse the half-duplex UART component where possible.

### WiFi

Initially focused on Pico W/Pico 2 W.

Possible responsibilities:

* WiFi initialization
* connection management
* credentials/configuration
* reconnect behavior
* network status

Higher-level TCP/UDP functionality should remain separable from WiFi connection management.

---

## 18. Possible Future Components

The framework may later grow to include:

```text
logging
ring_buffer
fifo
crc
watchdog
button
persistent_config
flash_storage
USB
TCP
UDP
HTTP
CAN
I2C device drivers
SPI device drivers
encoders
sensors
motor drivers
boot utilities
```

Components should only be added when there is a real reuse case.

The goal is not to build a complete general-purpose embedded operating system.

---

## 19. Testing

Testing starts with the first reusable component rather than being added after all components are ported.

Logic that does not require hardware, such as packet encoding, packet decoding, checksums, and CLI parsing, should be kept separate from Pico SDK calls and covered by host-side unit tests.

CI should at least:

* run host-side unit tests;
* build the `minimal` application for one RP2040 board and one RP2350 board;
* build representative application/profile combinations;
* report the SDK and toolchain versions used.

Hardware-dependent components should also have dedicated test applications.

Example:

```text
apps/tests/
├── ws2812_test/
├── cli_test/
├── ax12_test/
├── feetech_test/
└── wifi_test/
```

A test application should provide a minimal environment for validating one component on hardware.

This makes it possible to distinguish problems in a reusable component from problems in the larger application.

Hardware tests remain manual initially, with required board, wiring, expected output, and procedure documented beside each test application.

---

## 20. Logging and Diagnostics

A common lightweight logging mechanism may eventually be useful.

Possible interface:

```c
LOG_INFO("WiFi connected");
LOG_WARN("Servo timeout");
LOG_ERROR("Invalid packet");
```

The logging backend could later support:

* USB stdio
* UART
* disabled logging
* compile-time log levels

Logging should remain optional and lightweight.

---

## 21. Long-Term Repository Model

Initially, applications and components coexist in the same `pico-framework` repository. The initial implementation supports this standalone starter-project mode only: the top-level project initializes the bundled SDK and builds one selected application.

This makes development and refactoring easy.

Long term, the framework may also become a dependency of independent projects.

For example:

```text
robot-controller/
├── src/
├── config/
├── CMakeLists.txt
└── lib/
    └── pico-framework/
```

where `pico-framework` is itself a Git submodule.

Dependency mode should be added only when a real consuming project needs it. In that mode:

* the consuming project owns `project()`, Pico SDK initialization, and SDK version selection;
* `pico-framework` exposes namespaced component targets and does not add its bundled applications;
* including the framework must not initialize a second Pico SDK;
* standalone mode may continue using the SDK submodule pinned by this repository.

Architecture:

```text
project-specific firmware
        │
        ▼
  pico-framework
        │
 ┌──────┼─────────┐
 │      │         │
AX12   CLI      WS2812
 │
half-duplex UART
        │
        ▼
     Pico SDK
        │
        ▼
 RP2040 / RP2350
```

This would allow multiple projects to use the same maintained implementation of common components.

A bug fix to AX12 or WS2812 would then only need to be implemented once.

---

## 22. Non-Goals

The initial framework should explicitly avoid becoming too complex.

It should not initially attempt to provide:

* its own RTOS
* its own hardware abstraction layer replacing Pico SDK
* dynamic runtime module loading
* a complex package manager
* automatic dependency downloading
* a large generic driver framework
* extensive code generation
* complicated board description formats

The first implementation should favor readability, explicit CMake, and simple C APIs.

Complexity should only be added when repeated project experience demonstrates a need.

---

## 23. Target User Experience

The desired end result is that starting a new RP2040/RP2350 project changes from:

```text
find an old firmware project
copy it
remove unrelated files
copy newer drivers from another project
repair CMake
configure Pico SDK
change pins
discover that one copied driver is outdated
build
```

to:

```text
create project
select board
select application
select optional profile
configure hardware
write project-specific logic
build
flash
```

Example:

```bash
make BOARD=pico2 APP=servo_controller PROFILE=debug
make BOARD=pico2 APP=servo_controller PROFILE=debug flash
```

The framework should therefore act as both:

1. A Pico SDK starter template.
2. A reusable component library.
3. A common firmware platform for future RP2040/RP2350 projects.

---

## 24. Initial Implementation Priorities

The first implementation should focus on establishing the architecture rather than adding many components.

Recommended order:

1. Create repository structure.
2. Add Pico SDK as a pinned submodule.
3. Implement the `BOARD` / `APP` / `PROFILE` CMake model.
4. Create the `minimal` application and build it for RP2040 and RP2350.
5. Add the Makefile frontend with configuration-specific build directories.
6. Implement the component target convention.
7. Add the host-test harness and initial CI build matrix.
8. Port WS2812 and add its hardware test application.
9. Port the CLI parser with host-side tests, then add its transports.
10. Implement generic half-duplex UART and document its timing and ownership contract.
11. Port AX12 with host-side protocol tests and a hardware test application.
12. Port Feetech with the same test structure.
13. Add application profiles that are justified by real configurations.
14. Add WiFi support without committing credentials.
15. Document how to create a component, application, profile, and custom board.

All 15 initial priorities are implemented. This list is retained as the order
in which the framework was established; current implementation and physical
validation status live in the top-level README and the component test READMEs.

At that point the framework should already be useful for real projects, and additional architecture should be driven by actual needs rather than designed in advance.
