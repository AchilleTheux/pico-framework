# Hardware test for the SPI mcp2515 component. See README.md.

add_executable(app_mcp2515_test
    main.c
)

set(MCP2515_TEST_SPI_INSTANCE    1       CACHE STRING "SPI instance, 0 or 1")
set(MCP2515_TEST_SCK_PIN         10      CACHE STRING "GPIO connected to the controller's SCK")
set(MCP2515_TEST_MOSI_PIN        11      CACHE STRING "GPIO connected to the controller's SI (MOSI)")
set(MCP2515_TEST_MISO_PIN        12      CACHE STRING "GPIO connected to the controller's SO (MISO)")
set(MCP2515_TEST_CS_PIN          9       CACHE STRING "GPIO connected to the controller's CS")
set(MCP2515_TEST_INT_PIN         8       CACHE STRING "GPIO connected to the controller's INT; -1 to always poll")
set(MCP2515_TEST_SPI_BAUDRATE_HZ 1000000 CACHE STRING "SPI clock, Hz (the controller supports up to 10 MHz)")
set(MCP2515_TEST_OSCILLATOR_HZ   16000000 CACHE STRING "The controller's crystal/resonator frequency, Hz")
set(MCP2515_TEST_BITRATE         500000  CACHE STRING "CAN bus bitrate")
set(MCP2515_TEST_MODE            "normal" CACHE STRING "normal, loopback, or listen_only")
set_property(CACHE MCP2515_TEST_MODE PROPERTY STRINGS normal loopback listen_only)

if(MCP2515_TEST_MODE STREQUAL "normal")
    set(MCP2515_TEST_MODE_VALUE MCP2515_MODE_NORMAL)
elseif(MCP2515_TEST_MODE STREQUAL "loopback")
    set(MCP2515_TEST_MODE_VALUE MCP2515_MODE_LOOPBACK)
elseif(MCP2515_TEST_MODE STREQUAL "listen_only")
    set(MCP2515_TEST_MODE_VALUE MCP2515_MODE_LISTEN_ONLY)
else()
    message(FATAL_ERROR "MCP2515_TEST_MODE must be normal, loopback, or listen_only")
endif()

set(MCP2515_TEST_FILTER "none" CACHE STRING "Acceptance filter: none, id_123, or ext_only")
set_property(CACHE MCP2515_TEST_FILTER PROPERTY STRINGS none id_123 ext_only)

if(MCP2515_TEST_FILTER STREQUAL "none")
    set(MCP2515_TEST_FILTER_VALUE MCP2515_TEST_FILTER_NONE)
elseif(MCP2515_TEST_FILTER STREQUAL "id_123")
    set(MCP2515_TEST_FILTER_VALUE MCP2515_TEST_FILTER_ID_123)
elseif(MCP2515_TEST_FILTER STREQUAL "ext_only")
    set(MCP2515_TEST_FILTER_VALUE MCP2515_TEST_FILTER_EXT_ONLY)
else()
    message(FATAL_ERROR "MCP2515_TEST_FILTER must be none, id_123, or ext_only")
endif()

target_compile_definitions(app_mcp2515_test PRIVATE
    MCP2515_TEST_SPI_INSTANCE=${MCP2515_TEST_SPI_INSTANCE}
    MCP2515_TEST_SCK_PIN=${MCP2515_TEST_SCK_PIN}
    MCP2515_TEST_MOSI_PIN=${MCP2515_TEST_MOSI_PIN}
    MCP2515_TEST_MISO_PIN=${MCP2515_TEST_MISO_PIN}
    MCP2515_TEST_CS_PIN=${MCP2515_TEST_CS_PIN}
    MCP2515_TEST_INT_PIN=${MCP2515_TEST_INT_PIN}
    MCP2515_TEST_SPI_BAUDRATE_HZ=${MCP2515_TEST_SPI_BAUDRATE_HZ}
    MCP2515_TEST_OSCILLATOR_HZ=${MCP2515_TEST_OSCILLATOR_HZ}
    MCP2515_TEST_BITRATE=${MCP2515_TEST_BITRATE}
    MCP2515_TEST_MODE=${MCP2515_TEST_MODE_VALUE}
    MCP2515_TEST_FILTER=${MCP2515_TEST_FILTER_VALUE}
)

target_link_libraries(app_mcp2515_test PRIVATE
    pico_stdlib
    hardware_spi
    pico_framework::mcp2515
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_mcp2515_test 1)
pico_enable_stdio_uart(app_mcp2515_test 1)

pico_add_extra_outputs(app_mcp2515_test)
