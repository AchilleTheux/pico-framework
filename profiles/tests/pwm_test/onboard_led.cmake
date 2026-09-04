# GPIO 25 at 1 kHz: the onboard LED of a Pico or Pico 2, so `fade` needs no
# wiring at all. Not for a Pico W or Pico 2 W, whose LED hangs off the CYW43
# rather than a bank 0 pin and cannot be driven by PWM.

set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type")
set(PWM_TEST_GPIO         25   CACHE STRING "GPIO to drive")
set(PWM_TEST_SECOND_GPIO  24   CACHE STRING "Paired GPIO, for the slice-sharing tests")
set(PWM_TEST_FREQUENCY_HZ 1000 CACHE STRING "Starting frequency in Hz")
