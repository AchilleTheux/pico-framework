# MCP2515 (and pin/register-compatible clones, e.g. Waveshare's XL2515) SPI
# CAN controller.
#
# mcp2515_timing.c, mcp2515_frame.c and mcp2515_filters.c are SDK-independent
# and are compiled directly by the host tests.

add_library(pico_framework_mcp2515 STATIC
    mcp2515.c
    mcp2515_filters.c
    mcp2515_frame.c
    mcp2515_timing.c
)

add_library(pico_framework::mcp2515 ALIAS pico_framework_mcp2515)

target_include_directories(pico_framework_mcp2515 PUBLIC
    include
)

pico_framework_set_warnings(mcp2515.c mcp2515_filters.c mcp2515_frame.c mcp2515_timing.c)

target_link_libraries(pico_framework_mcp2515 PUBLIC
    pico_stdlib
    hardware_spi
    hardware_gpio
    pico_framework::can_frame
)
