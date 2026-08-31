# ST VL53L0X time-of-flight distance sensor.
#
# vl53l0x_timing.c is the sensor's timing arithmetic, free of the SDK and
# compiled straight into the host tests — which is where drivers for this part
# actually go wrong. vl53l0x.c is the register sequence.

add_library(pico_framework_vl53l0x STATIC
    vl53l0x_timing.c
    vl53l0x.c
)

add_library(pico_framework::vl53l0x ALIAS pico_framework_vl53l0x)

target_include_directories(pico_framework_vl53l0x PUBLIC
    include
)

pico_framework_set_warnings(vl53l0x_timing.c vl53l0x.c)

target_link_libraries(pico_framework_vl53l0x PUBLIC
    pico_framework::i2c_device
)
