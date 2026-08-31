/*
 * Project-local name for the Waveshare RP2040-Zero.
 *
 * The Pico SDK already carries the authoritative pin and flash definition as
 * waveshare_rp2040_zero. Keeping this thin alias avoids copying those facts and
 * gives projects the concise, stable BOARD=rp2040_zero spelling.
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_RP2040_ZERO_H
#define _BOARDS_RP2040_ZERO_H

pico_board_cmake_set(PICO_PLATFORM, rp2040)

#define RP2040_ZERO 1

#include "boards/waveshare_rp2040_zero.h"

#endif /* _BOARDS_RP2040_ZERO_H */
