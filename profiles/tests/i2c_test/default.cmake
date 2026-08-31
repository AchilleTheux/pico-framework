# i2c0 on GPIO 4/5 at 100 kHz, with the pads' internal pull-ups on so a sensor
# on a short lead works with no external resistors.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(I2C_TEST_INSTANCE 0      CACHE STRING "I2C instance")
set(I2C_TEST_SDA_PIN  4      CACHE STRING "SDA pin")
set(I2C_TEST_SCL_PIN  5      CACHE STRING "SCL pin")
set(I2C_TEST_BAUDRATE 100000 CACHE STRING "Bus rate")
set(I2C_TEST_INTERNAL_PULLUPS ON CACHE BOOL "Enable internal pull-ups")
