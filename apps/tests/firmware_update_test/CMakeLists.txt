# Runs the pure update-path logic on the target. See README.md.
#
# Its main job is keeping these components in the ARM build: they are pure, so
# nothing else links them yet, and without this app CI would only ever compile
# them for the host.

add_executable(app_firmware_update_test
    main.c
)

target_link_libraries(app_firmware_update_test PRIVATE
    pico_stdlib
    pico_framework::crc
    pico_framework::ring_buffer
    pico_framework::hex_parser
    pico_framework::firmware_update
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_firmware_update_test 1)
pico_enable_stdio_uart(app_firmware_update_test 1)

pico_add_extra_outputs(app_firmware_update_test)
