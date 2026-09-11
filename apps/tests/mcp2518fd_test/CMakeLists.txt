add_executable(app_mcp2518fd_test main.c)

set(MCP2518FD_TEST_SPI_INSTANCE 1 CACHE STRING "SPI instance: 0 or 1")
set(MCP2518FD_TEST_SCK_PIN 10 CACHE STRING "Controller SCK GPIO")
set(MCP2518FD_TEST_MOSI_PIN 11 CACHE STRING "Controller SDI GPIO")
set(MCP2518FD_TEST_MISO_PIN 12 CACHE STRING "Controller SDO GPIO")
set(MCP2518FD_TEST_CS_PIN 9 CACHE STRING "Controller CS GPIO")
set(MCP2518FD_TEST_INT_PIN -1 CACHE STRING "Controller INT GPIO; -1 to poll")
set(MCP2518FD_TEST_OSCILLATOR_HZ 40000000 CACHE STRING "Physical controller crystal frequency")
set(MCP2518FD_TEST_SPI_BAUDRATE_HZ 1000000 CACHE STRING "SPI clock in Hz")
set(MCP2518FD_TEST_BITRATE 500000 CACHE STRING "Nominal CAN bitrate")
set(MCP2518FD_TEST_DATA_BITRATE 2000000 CACHE STRING "CAN FD data bitrate")
option(MCP2518FD_TEST_FD "Enable CAN FD and send FD frames" OFF)
option(MCP2518FD_TEST_FILTER "Accept only standard ID 0x123" OFF)
set(MCP2518FD_TEST_MODE "normal" CACHE STRING "normal, loopback, or listen_only")
set_property(CACHE MCP2518FD_TEST_MODE PROPERTY STRINGS normal loopback listen_only)
if(MCP2518FD_TEST_MODE STREQUAL "normal")
    set(mode MCP2518FD_MODE_NORMAL)
elseif(MCP2518FD_TEST_MODE STREQUAL "loopback")
    set(mode MCP2518FD_MODE_LOOPBACK)
elseif(MCP2518FD_TEST_MODE STREQUAL "listen_only")
    set(mode MCP2518FD_MODE_LISTEN_ONLY)
else()
    message(FATAL_ERROR "MCP2518FD_TEST_MODE must be normal, loopback, or listen_only")
endif()
if(NOT MCP2518FD_TEST_SPI_INSTANCE MATCHES "^[01]$")
    message(FATAL_ERROR "MCP2518FD_TEST_SPI_INSTANCE must be 0 or 1")
endif()

foreach(setting SPI_INSTANCE SCK_PIN MOSI_PIN MISO_PIN CS_PIN INT_PIN
                OSCILLATOR_HZ SPI_BAUDRATE_HZ BITRATE DATA_BITRATE)
    target_compile_definitions(app_mcp2518fd_test PRIVATE
        MCP2518FD_TEST_${setting}=${MCP2518FD_TEST_${setting}})
endforeach()
target_compile_definitions(app_mcp2518fd_test PRIVATE
    MCP2518FD_TEST_MODE=${mode}
    MCP2518FD_TEST_FD=$<BOOL:${MCP2518FD_TEST_FD}>
    MCP2518FD_TEST_FILTER=$<BOOL:${MCP2518FD_TEST_FILTER}>
)
target_link_libraries(app_mcp2518fd_test PRIVATE pico_framework::mcp2518fd)
pico_framework_set_warnings(main.c)
pico_enable_stdio_usb(app_mcp2518fd_test 1)
pico_enable_stdio_uart(app_mcp2518fd_test 1)
pico_add_extra_outputs(app_mcp2518fd_test)
