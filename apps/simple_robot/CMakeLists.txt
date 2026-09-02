# ==============================================================================
# Simple Robot Application - CMake Build Configuration
# ==============================================================================
# In pico-framework, an application represents a complete standalone firmware.
# Exactly one application is compiled per build directory (selected via APP=...).
# Target names follow the convention: app_<directory_name> -> app_simple_robot.
# ==============================================================================

# Define the firmware executable target and its source files.
add_executable(app_simple_robot
    main.c
)

# ------------------------------------------------------------------------------
# Configuration Variables (CMake Cache Defaults)
# ------------------------------------------------------------------------------
# Declaring these as CACHE variables allows them to be overridden by profile files
# (e.g., profiles/simple_robot/rp2040_zero.cmake) or via -D command-line flags,
# without needing to modify this CMakeLists.txt.
# ------------------------------------------------------------------------------

# CLI UART Console Configuration (Dedicated hardware UART for CLI communication)
set(APP_CLI_UART_ID     0       CACHE STRING "CLI UART instance (0 for uart0, 1 for uart1)")
set(APP_CLI_TX_PIN      0       CACHE STRING "CLI UART TX GPIO pin")
set(APP_CLI_RX_PIN      1       CACHE STRING "CLI UART RX GPIO pin")
set(APP_CLI_BAUD        115200  CACHE STRING "CLI UART baud rate")
set(APP_CLI_STARTUP_DRAIN_MS 50 CACHE STRING "Time to discard UART startup noise")

# External WS2812 (NeoPixel) LED Strip Configuration
set(APP_LED_PIN         10      CACHE STRING "External WS2812 data GPIO pin")
set(APP_LED_COUNT       8       CACHE STRING "Number of WS2812 LEDs in the strip")

# Feetech Smart Serial Servo Bus Configuration (Half-duplex single-wire UART via PIO)
set(APP_SERVO_PIN       11      CACHE STRING "Feetech single-wire data GPIO pin")
set(APP_SERVO_BAUD      1000000 CACHE STRING "Feetech bus baud rate (typically 1 Mbps)")
option(APP_FEETECH_SCS  "Use the big-endian SCS protocol family instead of STS" OFF)

# Firmware build version or identifier stamp
set(APP_BUILD_STAMP "dev" CACHE STRING "Firmware build identifier string")

# ------------------------------------------------------------------------------
# Target Compile Definitions
# ------------------------------------------------------------------------------
# Expose the configuration variables above as C preprocessor macros (#define APP_...)
# to the application source code during compilation.
# ------------------------------------------------------------------------------
target_compile_definitions(app_simple_robot PRIVATE
    APP_CLI_UART_ID=${APP_CLI_UART_ID}
    APP_CLI_TX_PIN=${APP_CLI_TX_PIN}
    APP_CLI_RX_PIN=${APP_CLI_RX_PIN}
    APP_CLI_BAUD=${APP_CLI_BAUD}
    APP_CLI_STARTUP_DRAIN_MS=${APP_CLI_STARTUP_DRAIN_MS}

    APP_LED_PIN=${APP_LED_PIN}
    APP_LED_COUNT=${APP_LED_COUNT}

    APP_SERVO_PIN=${APP_SERVO_PIN}
    APP_SERVO_BAUD=${APP_SERVO_BAUD}
    APP_FEETECH_SCS=$<BOOL:${APP_FEETECH_SCS}>

    APP_BUILD_STAMP="${APP_BUILD_STAMP}"
)

# ------------------------------------------------------------------------------
# Component Dependencies & Libraries
# ------------------------------------------------------------------------------
# In pico-framework, reusable modules located in components/ are exposed as
# namespaced CMake library targets (pico_framework::<name>).
# Linking them here automatically resolves includes and transitive dependencies.
# ------------------------------------------------------------------------------
target_link_libraries(app_simple_robot PRIVATE
    pico_stdlib                     # Standard Pico SDK runtime (GPIO, UART, time, etc.)
    pico_framework::cli             # Interactive command-line interpreter (CLI)
    pico_framework::firmware_update # Serial in-application firmware updater (Intel HEX)
    pico_framework::feetech         # Feetech STS/SCS smart servo protocol (uses PIO UART)
    pico_framework::ws2812          # PIO-driven WS2812 / NeoPixel LED driver
)

# ------------------------------------------------------------------------------
# Standard I/O Routing
# ------------------------------------------------------------------------------
# Direct standard output (printf / stdout) to USB CDC serial so USB terminal
# debugging works out of the box, while leaving UART0 free for the dedicated CLI.
# ------------------------------------------------------------------------------
pico_enable_stdio_usb(app_simple_robot 1)
pico_enable_stdio_uart(app_simple_robot 0)

# The firmware updater erases and programs the staging/manifest regions, so
# keep the linked image inside the application half of the flash chip.
pico_framework_reserve_flash_layout(app_simple_robot)

# Apply framework compiler warning flags (-Wall, -Wextra, etc.)
pico_framework_set_warnings(main.c)

# Generate auxiliary outputs (.uf2, .hex, .bin, .map, .dis) alongside the .elf binary
pico_add_extra_outputs(app_simple_robot)
