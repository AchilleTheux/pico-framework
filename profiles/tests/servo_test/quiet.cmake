# AX-12, with everything below a warning compiled out — no code and no format
# strings in flash for the rest. What a release build would use.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(SERVO_TEST_FAMILY_FEETECH OFF CACHE BOOL "Talk to Feetech servos instead of AX-12")
set(SERVO_TEST_FEETECH_SCS    OFF CACHE BOOL "Feetech SCS series")
set(SERVO_TEST_STATUS_LEDS    OFF CACHE BOOL "Show transaction outcome on a WS2812 strip")
set(SERVO_TEST_PIN           21      CACHE STRING "Shared servo bus data pin")
set(SERVO_TEST_DIRECTION_PIN -1      CACHE STRING "Transceiver direction pin")
set(SERVO_TEST_BAUDRATE      1000000 CACHE STRING "Bus rate")
set(LOG_COMPILE_LEVEL "LOG_WARN_LEVEL" CACHE STRING "Lowest level compiled in")
