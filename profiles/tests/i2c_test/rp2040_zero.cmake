# The Waveshare RP2040-Zero's SDK-default I2C bus. Internal pull-ups make an
# empty-bus scan safe and useful without requiring any external wiring.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(I2C_TEST_INSTANCE 1      CACHE STRING "I2C instance")
set(I2C_TEST_SDA_PIN  6      CACHE STRING "SDA pin")
set(I2C_TEST_SCL_PIN  7      CACHE STRING "SCL pin")
set(I2C_TEST_BAUDRATE 100000 CACHE STRING "Bus rate")
set(I2C_TEST_INTERNAL_PULLUPS ON CACHE BOOL "Enable internal pull-ups")
