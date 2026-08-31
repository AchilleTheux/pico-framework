# Bench for the persistent_config component. See README.md.

add_executable(app_config_test
    main.c
)

target_link_libraries(app_config_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::persistent_config
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_config_test 1)
pico_enable_stdio_uart(app_config_test 1)

pico_add_extra_outputs(app_config_test)
