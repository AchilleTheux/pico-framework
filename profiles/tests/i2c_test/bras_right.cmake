# The right sensor bus of the bras_attrape_caisse board: i2c1 on GPIO 14/15.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(I2C_TEST_INSTANCE 1      CACHE STRING "I2C instance")
set(I2C_TEST_SDA_PIN  14     CACHE STRING "SDA pin")
set(I2C_TEST_SCL_PIN  15     CACHE STRING "SCL pin")
set(I2C_TEST_BAUDRATE 400000 CACHE STRING "Bus rate")
set(I2C_TEST_INTERNAL_PULLUPS OFF CACHE BOOL "Enable internal pull-ups")
