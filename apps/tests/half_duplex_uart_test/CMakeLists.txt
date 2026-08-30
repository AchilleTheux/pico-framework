# Hardware test for the half_duplex_uart component. See README.md.

add_executable(app_half_duplex_uart_test
    main.c
)

set(HDX_TEST_PIN           21      CACHE STRING "Shared data pin")
set(HDX_TEST_DIRECTION_PIN -1      CACHE STRING "Transceiver direction pin, -1 for none")
set(HDX_TEST_BAUDRATE      1000000 CACHE STRING "Bus rate")

target_compile_definitions(app_half_duplex_uart_test PRIVATE
    HDX_TEST_PIN=${HDX_TEST_PIN}
    HDX_TEST_DIRECTION_PIN=${HDX_TEST_DIRECTION_PIN}
    HDX_TEST_BAUDRATE=${HDX_TEST_BAUDRATE}
)

target_link_libraries(app_half_duplex_uart_test PRIVATE
    pico_stdlib
    pico_framework::half_duplex_uart
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_half_duplex_uart_test 1)
pico_enable_stdio_uart(app_half_duplex_uart_test 1)

pico_add_extra_outputs(app_half_duplex_uart_test)
