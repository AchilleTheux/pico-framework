# Interactive bench for the ax12 and feetech components. See README.md.
#
# One application, several profiles: which servo family is on the bus is a
# property of the bench, not of the code, so it is a profile rather than a
# second near-identical application.

add_executable(app_servo_test
    main.c
)

option(SERVO_TEST_FAMILY_FEETECH "Talk to Feetech servos instead of AX-12" OFF)
option(SERVO_TEST_FEETECH_SCS    "Feetech SCS series (big-endian) rather than STS" OFF)
option(SERVO_TEST_STATUS_LEDS    "Show transaction outcome on a WS2812 strip" OFF)
set(SERVO_TEST_PIN           21      CACHE STRING "Shared servo bus data pin")
set(SERVO_TEST_DIRECTION_PIN -1      CACHE STRING "Transceiver direction pin, -1 for none")
set(SERVO_TEST_BAUDRATE      1000000 CACHE STRING "Bus rate")
set(SERVO_TEST_LED_PIN       10      CACHE STRING "WS2812 data pin, when status LEDs are on")
set(SERVO_TEST_LED_COUNT     8       CACHE STRING "WS2812 strip length")

target_compile_definitions(app_servo_test PRIVATE
    SERVO_TEST_FAMILY_FEETECH=$<BOOL:${SERVO_TEST_FAMILY_FEETECH}>
    SERVO_TEST_FEETECH_SCS=$<BOOL:${SERVO_TEST_FEETECH_SCS}>
    SERVO_TEST_STATUS_LEDS=$<BOOL:${SERVO_TEST_STATUS_LEDS}>
    SERVO_TEST_PIN=${SERVO_TEST_PIN}
    SERVO_TEST_DIRECTION_PIN=${SERVO_TEST_DIRECTION_PIN}
    SERVO_TEST_BAUDRATE=${SERVO_TEST_BAUDRATE}
    SERVO_TEST_LED_PIN=${SERVO_TEST_LED_PIN}
    SERVO_TEST_LED_COUNT=${SERVO_TEST_LED_COUNT}
)

# Both servo components are linked so either profile builds from the same
# CMakeLists; --gc-sections drops whichever one the build did not call into.
target_link_libraries(app_servo_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::ax12
    pico_framework::feetech
    pico_framework::ws2812
    pico_framework::logging
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_servo_test 1)
pico_enable_stdio_uart(app_servo_test 1)

pico_add_extra_outputs(app_servo_test)
