# 25 kHz on GPIO 10: above hearing, which is where a brushed motor driver wants
# to be so the motor does not whine. At 125 MHz this is an exact frequency with
# 5000 counts per period.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(PWM_TEST_GPIO         10    CACHE STRING "GPIO to drive")
set(PWM_TEST_SECOND_GPIO  11    CACHE STRING "Paired GPIO, for the slice-sharing tests")
set(PWM_TEST_FREQUENCY_HZ 25000 CACHE STRING "Starting frequency in Hz")
