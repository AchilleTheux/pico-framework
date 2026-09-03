# Bench for the mqtt component. See README.md.
#
# Builds for any board; without a radio it says so and does nothing else, the
# same as wifi_test.

add_executable(app_mqtt_test
    main.c
)

target_link_libraries(app_mqtt_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::wifi
    pico_framework::mqtt
    pico_framework::json
    pico_framework::persistent_config
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_mqtt_test 1)
pico_enable_stdio_uart(app_mqtt_test 1)

# Saving settings writes persistent_config's reserved flash sectors.
pico_framework_reserve_flash_layout(app_mqtt_test)

pico_add_extra_outputs(app_mqtt_test)
