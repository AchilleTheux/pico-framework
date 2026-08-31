# Bench for the wifi component. See README.md.
#
# Builds for any board; without a radio it says so and does nothing else.

add_executable(app_wifi_test
    main.c
)

target_link_libraries(app_wifi_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::wifi
    pico_framework::persistent_config
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_wifi_test 1)
pico_enable_stdio_uart(app_wifi_test 1)

pico_add_extra_outputs(app_wifi_test)
