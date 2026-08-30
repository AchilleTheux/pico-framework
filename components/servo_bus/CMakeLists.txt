# Dynamixel Protocol 1.0 packets, and synchronous transactions over a
# half-duplex UART.
#
# servo_protocol.c is pure buffer and integer work with no SDK dependency, so
# the host tests compile it directly. servo_bus.c adds the transport.

add_library(pico_framework_servo_bus STATIC
    servo_protocol.c
    servo_bus.c
)

add_library(pico_framework::servo_bus ALIAS pico_framework_servo_bus)

target_include_directories(pico_framework_servo_bus PUBLIC
    include
)

pico_framework_set_warnings(servo_protocol.c servo_bus.c)

target_link_libraries(pico_framework_servo_bus PUBLIC
    pico_framework::half_duplex_uart
)
