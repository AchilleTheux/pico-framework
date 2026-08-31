# ==============================================================================
# simple_robot Profile: rp2040_zero.cmake
# ==============================================================================
# In pico-framework, a profile is an initial CMake cache preload script passed via
# `cmake -C profiles/<app>/<profile>.cmake` (or `make PROFILE=<profile>`).
#
# Profiles allow you to customize pin assignments, baud rates, feature flags,
# and build types for a specific board or robot variant WITHOUT modifying the
# application source code or its CMakeLists.txt.
# ==============================================================================

# Build type optimization level (Release = -O3, Debug = -g, RelWithDebInfo, etc.)
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")

# ------------------------------------------------------------------------------
# CLI Console UART Pinout & Baud Rate
# ------------------------------------------------------------------------------
set(APP_CLI_UART_ID 0       CACHE STRING "CLI UART instance (uart0)")
set(APP_CLI_TX_PIN  0       CACHE STRING "CLI UART TX pin (GPIO 0)")
set(APP_CLI_RX_PIN  1       CACHE STRING "CLI UART RX pin (GPIO 1)")
set(APP_CLI_BAUD    115200  CACHE STRING "CLI UART baud rate (115200 bps)")
set(APP_CLI_STARTUP_DRAIN_MS 50 CACHE STRING "Discard FTDI startup noise for this long")

# ------------------------------------------------------------------------------
# WS2812 RGB LED Strip Configuration
# ------------------------------------------------------------------------------
set(APP_LED_PIN   10        CACHE STRING "WS2812 data signal GPIO pin")
set(APP_LED_COUNT 8         CACHE STRING "WS2812 number of RGB LEDs in the strip")

# ------------------------------------------------------------------------------
# Feetech Smart Serial Servo Bus Configuration
# ------------------------------------------------------------------------------
set(APP_SERVO_PIN  11       CACHE STRING "Feetech single-wire data GPIO pin")
set(APP_SERVO_BAUD 1000000  CACHE STRING "Feetech bus baud rate (1 Mbps)")
set(APP_FEETECH_SCS OFF     CACHE BOOL "Use SCS (big-endian) protocol instead of STS (little-endian)")

# ------------------------------------------------------------------------------
# Application Metadata
# ------------------------------------------------------------------------------
set(APP_BUILD_STAMP "A"     CACHE STRING "Firmware build identifier / revision stamp")

# ------------------------------------------------------------------------------
# Firmware Update Service Configuration
# ------------------------------------------------------------------------------
# Enable fwapply command in the firmware update service. When ON, allows the
# application to copy a staged firmware image over the active partition in flash
# and reboot into the new firmware.
set(FIRMWARE_SERVICE_ENABLE_APPLY ON CACHE BOOL
    "Allow the running firmware to install a staged image")
