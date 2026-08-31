# A device on an I2C bus, at an address.
#
# i2c_bytes.c is the byte order, free of the SDK and compiled straight into the
# host tests. i2c_device.c is the transfers.

add_library(pico_framework_i2c_device STATIC
    i2c_bytes.c
    i2c_device.c
)

add_library(pico_framework::i2c_device ALIAS pico_framework_i2c_device)

target_include_directories(pico_framework_i2c_device PUBLIC
    include
)

pico_framework_set_warnings(i2c_bytes.c i2c_device.c)

target_link_libraries(pico_framework_i2c_device PUBLIC
    pico_stdlib
    hardware_i2c
)
