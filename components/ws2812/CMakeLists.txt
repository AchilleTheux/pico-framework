# WS2812 / WS2812B / SK6812 LED strip driver.

add_library(pico_framework_ws2812 STATIC
    ws2812.c
    ws2812_color.c
)

add_library(pico_framework::ws2812 ALIAS pico_framework_ws2812)

target_include_directories(pico_framework_ws2812 PUBLIC
    include
)

# Generates ws2812.pio.h into the build tree and puts it on the include path.
pico_framework_set_warnings(ws2812.c ws2812_color.c)

pico_generate_pio_header(pico_framework_ws2812
    ${CMAKE_CURRENT_LIST_DIR}/ws2812.pio
)

target_link_libraries(pico_framework_ws2812 PUBLIC
    pico_stdlib
    hardware_pio
    hardware_clocks
)
