# The framework's CLI, over Bluetooth. See README.md.
#
# Builds for any board; without a radio it says so over USB and does nothing
# else.

add_executable(app_bt_console_test
    main.c
)

target_link_libraries(app_bt_console_test PRIVATE
    pico_stdlib
    pico_framework::cli
    pico_framework::bluetooth
)

# Linking wifi as well proves the two can share one radio: only one cyw43_arch
# may exist, so PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI decides which provides it.
if (PICO_FRAMEWORK_BLUETOOTH_WITH_WIFI)
    target_link_libraries(app_bt_console_test PRIVATE pico_framework::wifi)
endif()

pico_framework_set_warnings(main.c)

# USB stdio only. A UART would work too, but the point of this application is
# that the console arrives over Bluetooth; stdio is only used to report whether
# Bluetooth came up at all.
pico_enable_stdio_usb(app_bt_console_test 1)
pico_enable_stdio_uart(app_bt_console_test 0)

pico_add_extra_outputs(app_bt_console_test)
