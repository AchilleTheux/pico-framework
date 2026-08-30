# Dynamixel AX-12 / AX-18 servos.
#
# The control table and its conversions are in ax12_registers.c, which has no
# SDK dependency and is compiled straight into the host tests.

add_library(pico_framework_ax12 STATIC
    ax12.c
    ax12_registers.c
)

add_library(pico_framework::ax12 ALIAS pico_framework_ax12)

target_include_directories(pico_framework_ax12 PUBLIC
    include
)

pico_framework_set_warnings(ax12.c ax12_registers.c)

# servo_bus brings half_duplex_uart with it, so an application linking ax12
# never has to name the transport (DESIGN_DOC.md section 7).
target_link_libraries(pico_framework_ax12 PUBLIC
    pico_framework::servo_bus
)
