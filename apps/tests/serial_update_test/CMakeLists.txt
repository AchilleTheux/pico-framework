# A firmware that can be replaced over its own console. See README.md.

add_executable(app_serial_update_test
    main.c
)

# Distinguishes one build from the next, so an update can be seen to have
# taken effect. Set it to something recognisable when testing.
set(SERIAL_UPDATE_BUILD_STAMP "dev" CACHE STRING
    "Build stamp reported by the version command")

target_compile_definitions(app_serial_update_test PRIVATE
    SERIAL_UPDATE_BUILD_STAMP="${SERIAL_UPDATE_BUILD_STAMP}"
)

target_link_libraries(app_serial_update_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::firmware_update
)

pico_framework_set_warnings(main.c)

pico_enable_stdio_usb(app_serial_update_test 1)
pico_enable_stdio_uart(app_serial_update_test 1)

# Fail the link rather than the update: this application stages an image into
# the region above its own, so an image that grew past the application/staging
# boundary would have it erase live code.
pico_framework_reserve_flash_layout(app_serial_update_test)

# The .hex is what gets sent over the wire, so make sure it is produced.
pico_add_extra_outputs(app_serial_update_test)
