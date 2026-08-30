# 8N1 UART over a single shared wire, for smart-servo buses.
#
# half_duplex_uart_timing.c is pure arithmetic with no SDK dependency, so the
# host tests compile it directly.

add_library(pico_framework_half_duplex_uart STATIC
    half_duplex_uart.c
    half_duplex_uart_timing.c
)

add_library(pico_framework::half_duplex_uart ALIAS pico_framework_half_duplex_uart)

target_include_directories(pico_framework_half_duplex_uart PUBLIC
    include
)

pico_framework_set_warnings(half_duplex_uart.c half_duplex_uart_timing.c)

pico_generate_pio_header(pico_framework_half_duplex_uart
    ${CMAKE_CURRENT_LIST_DIR}/half_duplex_uart.pio
)

target_link_libraries(pico_framework_half_duplex_uart PUBLIC
    pico_stdlib
    hardware_pio
    hardware_clocks
)
