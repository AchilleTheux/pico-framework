# A board with a transceiver or level shifter steered by GPIO 27, matching the
# Carte_actionneurs wiring the component was ported from.

set(CMAKE_BUILD_TYPE       "Release" CACHE STRING "Build type")
set(HDX_TEST_PIN           21        CACHE STRING "Shared data pin")
set(HDX_TEST_DIRECTION_PIN 27        CACHE STRING "Transceiver direction pin, -1 for none")
set(HDX_TEST_BAUDRATE      1000000   CACHE STRING "Bus rate")
