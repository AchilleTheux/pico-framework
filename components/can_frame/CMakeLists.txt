# CAN identifiers, frame validation, filters, and the interrupt-to-main-loop
# queue. SDK-independent so it is compiled straight into the host tests.
#
# Split out of components/can so an SPI controller driver (components/mcp2515)
# can share the same can_message_t/can_filter_t types without pulling in
# can2040, PIO, or that dependency's GPLv3 license.

add_library(pico_framework_can_frame STATIC
    can_frame.c
)

add_library(pico_framework::can_frame ALIAS pico_framework_can_frame)

target_include_directories(pico_framework_can_frame PUBLIC
    include
)

pico_framework_set_warnings(can_frame.c)

target_link_libraries(pico_framework_can_frame PUBLIC
    pico_stdlib
    pico_framework::ring_buffer
)
