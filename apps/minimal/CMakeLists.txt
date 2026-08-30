# The minimal application: the smallest complete firmware the framework builds.
#
# It exists to prove the BOARD / APP / PROFILE model end to end on both RP2040
# and RP2350, and to serve as the starting point for a new application.
# It links no framework components on purpose.

add_executable(app_minimal
    main.c
)

# Where stdio goes. Overridable from a profile; see profiles/minimal/.
option(MINIMAL_STDIO_USB  "Route stdio to USB CDC" ON)
option(MINIMAL_STDIO_UART "Route stdio to the board's default UART" ON)

target_link_libraries(app_minimal PRIVATE
    pico_stdlib
)

pico_enable_stdio_usb(app_minimal ${MINIMAL_STDIO_USB})
pico_enable_stdio_uart(app_minimal ${MINIMAL_STDIO_UART})

# .uf2 / .hex / .bin / .dis / .map beside the .elf
pico_add_extra_outputs(app_minimal)
