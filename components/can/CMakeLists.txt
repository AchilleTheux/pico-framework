# CAN 2.0B controller implemented by can2040 on one complete PIO block.
#
# Frame/filter/queue types live in the can_frame component, shared with the
# SPI-based mcp2515 controller driver, so that GPLv3 can2040 dependency stays
# scoped to applications that actually need the PIO backend.

set(CAN2040_SOURCE "${PICO_FRAMEWORK_ROOT}/lib/can2040/src/can2040.c")

add_library(pico_framework_can STATIC
    can.c
    "${CAN2040_SOURCE}"
)

add_library(pico_framework::can ALIAS pico_framework_can)

target_include_directories(pico_framework_can PUBLIC
    include
    "${PICO_FRAMEWORK_ROOT}/lib/can2040/src"
)

pico_framework_set_warnings(can.c)

# can2040's timing requirements explicitly require optimisation even when the
# surrounding application is a Debug build.
set_source_files_properties("${CAN2040_SOURCE}" PROPERTIES COMPILE_OPTIONS "-O2")

target_link_libraries(pico_framework_can PUBLIC
    pico_stdlib
    hardware_clocks
    hardware_irq
    hardware_pio
    pico_framework::can_frame
)
