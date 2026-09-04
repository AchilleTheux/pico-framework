# 1 kHz on GPIO 10: fast enough that an LED looks steady rather than flickering,
# slow enough that a scope trigger is easy.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(PWM_TEST_GPIO         10   CACHE STRING "GPIO to drive")
set(PWM_TEST_SECOND_GPIO  11   CACHE STRING "Paired GPIO, for the slice-sharing tests")
set(PWM_TEST_FREQUENCY_HZ 1000 CACHE STRING "Starting frequency in Hz")
