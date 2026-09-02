/*
 * Board header for the Waveshare RP2350-CAN.
 *
 * RP2350A module with an onboard XL2515 (MCP2515-compatible) SPI CAN
 * controller and a status LED. Confirmed against
 * docs/RP2350-CAN-Schematic.pdf: a 32 Mbit (4 MiB) P25Q32SH-UXH-IR flash
 * chip, command-compatible with the generic W25Q080 stage2 boot config
 * below (same as boards/pico2.h in the SDK uses for its own flash).
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_RP2350_CAN_H
#define _BOARDS_RP2350_CAN_H

// --- Target Platform ---
pico_board_cmake_set(PICO_PLATFORM, rp2350)

// Board identification macro
#define RP2350_CAN 1

// --- RP2350 Variant ---
#ifndef PICO_RP2350A
#define PICO_RP2350A 1
#endif

// --- Flash Configuration ---
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (4 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (4 * 1024 * 1024)
#endif

// --- Debug UART (GPIO0/1, standard Pico-footprint debug header) ---
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 0
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 1
#endif

// --- Onboard status LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- Onboard XL2515 (MCP2515-compatible) CAN controller, SPI1 ---
// SCLK=GP10, MOSI(TX)=GP11, MISO(RX)=GP12, CS=GP9 are SPI1's native
// alternate-function pins. GP8 (INT) is a plain GPIO interrupt input wired
// to the controller's active-low INT pin, not used in its SPI1 RX
// alternate function.
#ifndef PICO_DEFAULT_SPI
#define PICO_DEFAULT_SPI 1
#endif
#ifndef PICO_DEFAULT_SPI_SCK_PIN
#define PICO_DEFAULT_SPI_SCK_PIN 10
#endif
#ifndef PICO_DEFAULT_SPI_TX_PIN
#define PICO_DEFAULT_SPI_TX_PIN 11
#endif
#ifndef PICO_DEFAULT_SPI_RX_PIN
#define PICO_DEFAULT_SPI_RX_PIN 12
#endif
#ifndef PICO_DEFAULT_SPI_CSN_PIN
#define PICO_DEFAULT_SPI_CSN_PIN 9
#endif

#ifndef RP2350_CAN_PIN_XL2515_INT
#define RP2350_CAN_PIN_XL2515_INT 8
#endif

// Confirmed against docs/RP2350-CAN-Schematic.pdf: Y1, the XL2515's crystal,
// is 16 MHz (not the 8 MHz common on smaller MCP2515 breakout modules).
#ifndef RP2350_CAN_XL2515_OSCILLATOR_HZ
#define RP2350_CAN_XL2515_OSCILLATOR_HZ 16000000
#endif

#endif // _BOARDS_RP2350_CAN_H
