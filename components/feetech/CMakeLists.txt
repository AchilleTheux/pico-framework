# Feetech STS / SMS / SCS smart servos.
#
# The control table and its conversions are in feetech_registers.c, which has
# no SDK dependency and is compiled straight into the host tests.

add_library(pico_framework_feetech STATIC
    feetech.c
    feetech_registers.c
)

add_library(pico_framework::feetech ALIAS pico_framework_feetech)

target_include_directories(pico_framework_feetech PUBLIC
    include
)

pico_framework_set_warnings(feetech.c feetech_registers.c)

target_link_libraries(pico_framework_feetech PUBLIC
    pico_framework::servo_bus
)
