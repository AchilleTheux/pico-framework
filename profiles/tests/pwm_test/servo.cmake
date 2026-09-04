# 50 Hz on GPIO 10: the RC servo and ESC frame. `pulse 1500` centres a servo,
# and `servo` sweeps it.
#
# A servo needs its own 5-6 V supply with its ground tied to the board's; the
# signal pin is the only wire that comes from the Pico.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(PWM_TEST_GPIO         10  CACHE STRING "GPIO to drive")
set(PWM_TEST_SECOND_GPIO  11  CACHE STRING "Paired GPIO, for the slice-sharing tests")
set(PWM_TEST_FREQUENCY_HZ 50  CACHE STRING "Starting frequency in Hz")
