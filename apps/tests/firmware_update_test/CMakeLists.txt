# Runs the pure update-path logic on the target. See README.md.
#
# Its main job is keeping these components in the ARM build: they are pure, so
# nothing else links them yet, and without this app CI would only ever compile
# them for the host.

add_executable(app_firmware_update_test
    main.c
)

option(FIRMWARE_UPDATE_TEST_WRITE_FLASH
       "Also erase and program a staging sector (destructive)" OFF)

target_compile_definitions(app_firmware_update_test PRIVATE
    FIRMWARE_UPDATE_TEST_WRITE_FLASH=$<BOOL:${FIRMWARE_UPDATE_TEST_WRITE_FLASH}>
)

target_link_libraries(app_firmware_update_test PRIVATE
    pico_stdlib
    pico_framework::flash_storage
    pico_framework::crc
    pico_framework::ring_buffer
    pico_framework::hex_parser
    pico_framework::firmware_update
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_firmware_update_test 1)
pico_enable_stdio_uart(app_firmware_update_test 1)

# Fail the link rather than the update: this application stages an image into
# the region above its own, so an image that grew past the application/staging
# boundary would have it erase live code.
pico_framework_reserve_flash_layout(app_firmware_update_test)

pico_add_extra_outputs(app_firmware_update_test)
