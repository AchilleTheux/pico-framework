# Hardware test for the ws2812 component. See README.md for wiring.

add_executable(app_ws2812_test
    main.c
)

# Strip geometry is a property of the bench setup, so it comes from the
# profile rather than being baked into the source.
set(WS2812_TEST_PIN     10 CACHE STRING "GPIO driving the strip's data line")
set(WS2812_TEST_LENGTH  16 CACHE STRING "Number of LEDs on the strip")
option(WS2812_TEST_IS_RGBW "Strip has a dedicated white LED (SK6812 RGBW)" OFF)
option(WS2812_TEST_USE_DMA "Give the strip a wire buffer and exercise the DMA path" OFF)
option(WS2812_TEST_VISUAL_CONFIRM "Loop over one-second blue/red/green/off steps" OFF)

target_compile_definitions(app_ws2812_test PRIVATE
    WS2812_TEST_PIN=${WS2812_TEST_PIN}
    WS2812_TEST_LENGTH=${WS2812_TEST_LENGTH}
    WS2812_TEST_IS_RGBW=$<BOOL:${WS2812_TEST_IS_RGBW}>
    WS2812_TEST_USE_DMA=$<BOOL:${WS2812_TEST_USE_DMA}>
    WS2812_TEST_VISUAL_CONFIRM=$<BOOL:${WS2812_TEST_VISUAL_CONFIRM}>
)

target_link_libraries(app_ws2812_test PRIVATE
    pico_stdlib
    pico_framework::ws2812
)

pico_enable_stdio_usb(app_ws2812_test 1)
pico_enable_stdio_uart(app_ws2812_test 1)

pico_framework_set_warnings(main.c)

pico_add_extra_outputs(app_ws2812_test)
