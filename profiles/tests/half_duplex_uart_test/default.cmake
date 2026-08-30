# Loopback on a bare pin at 1 Mbaud, the rate AX-12 and Feetech buses use.
# Needs nothing connected: the receiver hears the transmitter.

set(CMAKE_BUILD_TYPE       "Release" CACHE STRING "Build type")
set(HDX_TEST_PIN           21        CACHE STRING "Shared data pin")
set(HDX_TEST_DIRECTION_PIN -1        CACHE STRING "Transceiver direction pin, -1 for none")
set(HDX_TEST_BAUDRATE      1000000   CACHE STRING "Bus rate")
