# The left sensor bus of the bras_attrape_caisse board: i2c0 on GPIO 12/13.
# That board has its own pull-ups, so the pads' weak ones are left off.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(I2C_TEST_INSTANCE 0      CACHE STRING "I2C instance")
set(I2C_TEST_SDA_PIN  12     CACHE STRING "SDA pin")
set(I2C_TEST_SCL_PIN  13     CACHE STRING "SCL pin")
set(I2C_TEST_BAUDRATE 400000 CACHE STRING "Bus rate")
set(I2C_TEST_INTERNAL_PULLUPS OFF CACHE BOOL "Enable internal pull-ups")
