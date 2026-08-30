# AX-12 with a WS2812 strip showing whether the last transaction worked.
# Demonstrates composing cli, half_duplex_uart, ax12 and ws2812 in one app.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(SERVO_TEST_FAMILY_FEETECH OFF CACHE BOOL "Talk to Feetech servos instead of AX-12")
set(SERVO_TEST_FEETECH_SCS    OFF CACHE BOOL "Feetech SCS series (big-endian) rather than STS")
set(SERVO_TEST_STATUS_LEDS    ON CACHE BOOL "Show transaction outcome on a WS2812 strip")
set(SERVO_TEST_PIN           21      CACHE STRING "Shared servo bus data pin")
set(SERVO_TEST_DIRECTION_PIN -1      CACHE STRING "Transceiver direction pin, -1 for none")
set(SERVO_TEST_BAUDRATE      1000000 CACHE STRING "Bus rate")
