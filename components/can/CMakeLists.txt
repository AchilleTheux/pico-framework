# CAN 2.0B controller implemented by can2040 on one complete PIO block.
#
# can_frame.c is SDK-independent and is compiled directly by the host tests.

set(CAN2040_SOURCE "${PICO_FRAMEWORK_ROOT}/lib/can2040/src/can2040.c")

add_library(pico_framework_can STATIC
    can.c
    can_frame.c
    "${CAN2040_SOURCE}"
)

add_library(pico_framework::can ALIAS pico_framework_can)

target_include_directories(pico_framework_can PUBLIC
    include
    "${PICO_FRAMEWORK_ROOT}/lib/can2040/src"
)

pico_framework_set_warnings(can.c can_frame.c)

# can2040's timing requirements explicitly require optimisation even when the
# surrounding application is a Debug build.
set_source_files_properties("${CAN2040_SOURCE}" PROPERTIES COMPILE_OPTIONS "-O2")

target_link_libraries(pico_framework_can PUBLIC
    pico_stdlib
    hardware_clocks
    hardware_irq
    hardware_pio
    pico_framework::ring_buffer
)
