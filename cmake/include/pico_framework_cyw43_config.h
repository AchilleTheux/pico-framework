/*
 * CYW43 port configuration for Pico SDK 2.3.0 on RP2350.
 *
 * The standard Pico port supplies every setting first.  Only its two short
 * low-power waits are replaced: SDK 2.3.0 can lose their hardware-alarm
 * wakeup on RP2350, turning a nominal 1 ms wait into an unbounded one.  A
 * busy wait costs power only while an ioctl or flow-control send is already
 * blocking the caller, and preserves the driver's intended 1 ms cadence.
 *
 * CMake selects this file only for the affected SDK/platform combination.
 */

#ifndef PICO_FRAMEWORK_CYW43_CONFIG_H
#define PICO_FRAMEWORK_CYW43_CONFIG_H

#include "cyw43_configport.h"

#undef CYW43_SDPCM_SEND_COMMON_WAIT
#define CYW43_SDPCM_SEND_COMMON_WAIT busy_wait_us_32(1000)

#undef CYW43_DO_IOCTL_WAIT
#define CYW43_DO_IOCTL_WAIT busy_wait_us_32(1000)

#endif /* PICO_FRAMEWORK_CYW43_CONFIG_H */
