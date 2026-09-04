# Hardware test for the PIO can2040 component. See README.md.

add_executable(app_can_test
    main.c
)

set(CAN_TEST_PIO     0      CACHE STRING "PIO block (0, 1, or RP2350-only 2)")
set(CAN_TEST_RX_PIN  4      CACHE STRING "GPIO connected to transceiver RXD")
set(CAN_TEST_TX_PIN  5      CACHE STRING "GPIO connected to transceiver TXD; -1 for monitor")
set(CAN_TEST_BITRATE 500000 CACHE STRING "CAN bus bitrate")
set(CAN_TEST_FILTER  "none" CACHE STRING "Acceptance filter: none, id_123, or ext_only")
set_property(CACHE CAN_TEST_FILTER PROPERTY STRINGS none id_123 ext_only)

if(CAN_TEST_FILTER STREQUAL "none")
    set(CAN_TEST_FILTER_VALUE CAN_TEST_FILTER_NONE)
elseif(CAN_TEST_FILTER STREQUAL "id_123")
    set(CAN_TEST_FILTER_VALUE CAN_TEST_FILTER_ID_123)
elseif(CAN_TEST_FILTER STREQUAL "ext_only")
    set(CAN_TEST_FILTER_VALUE CAN_TEST_FILTER_EXT_ONLY)
else()
    message(FATAL_ERROR "CAN_TEST_FILTER must be none, id_123, or ext_only")
endif()

target_compile_definitions(app_can_test PRIVATE
    CAN_TEST_PIO=${CAN_TEST_PIO}
    CAN_TEST_RX_PIN=${CAN_TEST_RX_PIN}
    CAN_TEST_TX_PIN=${CAN_TEST_TX_PIN}
    CAN_TEST_BITRATE=${CAN_TEST_BITRATE}
    CAN_TEST_FILTER=${CAN_TEST_FILTER_VALUE}
)

target_link_libraries(app_can_test PRIVATE
    pico_stdlib
    pico_framework::can
)

# can2040's IRQ deadline is short enough that flash cache misses matter.
pico_set_binary_type(app_can_test copy_to_ram)

pico_enable_stdio_usb(app_can_test 1)
pico_enable_stdio_uart(app_can_test 1)

pico_framework_set_warnings(main.c)
pico_add_extra_outputs(app_can_test)
