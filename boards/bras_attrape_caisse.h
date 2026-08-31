/*
 * Copyright (c) 2026 Club Robot INP-ENSEEIHT (7Robot)
 * SPDX-License-Identifier: BSD-3-Clause
 *
 * Board header for "Bras Attrape Caisse" (Eurobot 2026 actuator board)
 * Generated from KiCad schematic: bras_attrape_caisse_schematic.pdf
 */

// -----------------------------------------------------
// NOTE: THIS HEADER IS ALSO INCLUDED BY ASSEMBLER SO
//       SHOULD ONLY CONSIST OF PREPROCESSOR DIRECTIVES
// -----------------------------------------------------

#ifndef _BOARDS_BRAS_ATTRAPE_CAISSE_H
#define _BOARDS_BRAS_ATTRAPE_CAISSE_H

// --- Target Platform ---
pico_board_cmake_set(PICO_PLATFORM, rp2040)

// Board identification macro
#define BRAS_ATTRAPE_CAISSE 1

// --- Flash Configuration ---
// Standard 2MB flash on Raspberry Pi Pico module (Winbond W25Q16JV / compatible)
#define PICO_BOOT_STAGE2_CHOOSE_W25Q080 1

#ifndef PICO_FLASH_SPI_CLKDIV
#define PICO_FLASH_SPI_CLKDIV 2
#endif

pico_board_cmake_set_default(PICO_FLASH_SIZE_BYTES, (2 * 1024 * 1024))
#ifndef PICO_FLASH_SIZE_BYTES
#define PICO_FLASH_SIZE_BYTES (2 * 1024 * 1024)
#endif

#ifndef PICO_RP2040_B0_SUPPORTED
#define PICO_RP2040_B0_SUPPORTED 1
#endif

// --- Default Pico On-board LED ---
#ifndef PICO_DEFAULT_LED_PIN
#define PICO_DEFAULT_LED_PIN 25
#endif

// --- Primary Communication UART (Host / Debug on J39 connector) ---
// Connected to RP2040 UART0: TX=GPIO16, RX=GPIO17
#ifndef PICO_DEFAULT_UART
#define PICO_DEFAULT_UART 0
#endif
#ifndef PICO_DEFAULT_UART_TX_PIN
#define PICO_DEFAULT_UART_TX_PIN 16
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#define PICO_DEFAULT_UART_RX_PIN 17
#endif

// --- I2C Sensors (Color Sensors + TOF) ---
// Bus 1 (capteur_droite on J1, J2): RP2040 I2C1 (SDA=GPIO14, SCL=GPIO15)
#ifndef PICO_DEFAULT_I2C
#define PICO_DEFAULT_I2C 1
#endif
#ifndef PICO_DEFAULT_I2C_SDA_PIN
#define PICO_DEFAULT_I2C_SDA_PIN 14
#endif
#ifndef PICO_DEFAULT_I2C_SCL_PIN
#define PICO_DEFAULT_I2C_SCL_PIN 15
#endif

// Bus 2 (capteur_gauche on J6, J7): RP2040 I2C0 (SDA=GPIO12, SCL=GPIO13)
#ifndef BRAS_I2C_LEFT_INSTANCE
#define BRAS_I2C_LEFT_INSTANCE 0
#endif
#ifndef BRAS_I2C_LEFT_SDA_PIN
#define BRAS_I2C_LEFT_SDA_PIN 12
#endif
#ifndef BRAS_I2C_LEFT_SCL_PIN
#define BRAS_I2C_LEFT_SCL_PIN 13
#endif

#ifndef BRAS_I2C_RIGHT_INSTANCE
#define BRAS_I2C_RIGHT_INSTANCE 1
#endif
#ifndef BRAS_I2C_RIGHT_SDA_PIN
#define BRAS_I2C_RIGHT_SDA_PIN 14
#endif
#ifndef BRAS_I2C_RIGHT_SCL_PIN
#define BRAS_I2C_RIGHT_SCL_PIN 15
#endif

// --- Servo Busses ---
// Feetech STS/SCS servos (Single-wire Half-Duplex UART on J12, J13, J14, J43, J44)
#ifndef BRAS_PIN_FEETECH_DATA
#define BRAS_PIN_FEETECH_DATA 28
#endif

// Dynamixel AX-12 servos (Half-Duplex UART via SN74LVC1T45 level shifter on J15)
#ifndef BRAS_PIN_AX12_DATA
#define BRAS_PIN_AX12_DATA 21
#endif
#ifndef BRAS_PIN_AX12_DIR
#define BRAS_PIN_AX12_DIR 27
#endif

// --- Actuators (Pumps & Solenoid Valves via MOSFET switches to 12V Vdrive) ---
// Pump 1 (Pompe 1 on J10)
#ifndef BRAS_PIN_PUMP1
#define BRAS_PIN_PUMP1 6
#endif
// Solenoid Valve 1 (Electrovanne 1 on J8)
#ifndef BRAS_PIN_SOLENOID1
#define BRAS_PIN_SOLENOID1 7
#endif
// Pump 2 (Pompe 2 on J11)
#ifndef BRAS_PIN_PUMP2
#define BRAS_PIN_PUMP2 8
#endif
// Solenoid Valve 2 (Electrovanne 2 on J9)
#ifndef BRAS_PIN_SOLENOID2
#define BRAS_PIN_SOLENOID2 9
#endif

// --- WS2812 Addressable RGB LED Strips ---
// RGB Strip 1 (J21 connector)
#ifndef BRAS_PIN_WS2812_STRIP1
#define BRAS_PIN_WS2812_STRIP1 11
#endif
// RGB Strip 2 (J22 connector)
#ifndef BRAS_PIN_WS2812_STRIP2
#define BRAS_PIN_WS2812_STRIP2 10
#endif

// --- Digital & Analog Inputs / Sensors ---
// Photoelectric sensor (Capteur photo-electrique du barillet on J5)
#ifndef BRAS_PIN_PHOTO_ELEC
#define BRAS_PIN_PHOTO_ELEC 18
#endif

// Battery Voltage Sensing (Resistor divider 100k/10k, ratio ~1:11)
#ifndef BRAS_PIN_VBAT_SENSE
#define BRAS_PIN_VBAT_SENSE 20
#endif

// --- Buttons & Safety Inputs ---
// Emergency Stop Status (Arret d'Urgence active-low input: 0 = emergency stop active)
#ifndef BRAS_PIN_AU
#define BRAS_PIN_AU 2
#endif

// Tether / Cord Safety Button (Bouton Laisse on J19)
#ifndef BRAS_PIN_BT_LAISSE
#define BRAS_PIN_BT_LAISSE 3
#endif

// Push Button 1 (Bouton Poussoir 1 on J3)
#ifndef BRAS_PIN_BTN1
#define BRAS_PIN_BTN1 26
#endif

// Push Button 2 (Bouton Poussoir 2 on J4)
#ifndef BRAS_PIN_BTN2
#define BRAS_PIN_BTN2 22
#endif

// --- Power Control ---
// Enable 12V Vdrive DC/DC converter (PTN78000H)
#ifndef BRAS_PIN_EN_VDRIVE
#define BRAS_PIN_EN_VDRIVE 19
#endif

#endif // _BOARDS_BRAS_ATTRAPE_CAISSE_H
