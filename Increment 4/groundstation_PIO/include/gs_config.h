// =============================================================================
// gs_config.h - ground station (NodeMCU-32S) pins and link settings
// =============================================================================
// Pins: Increment 2 RX node wiring (docs/02_wiring.md, board_pins.h) = ICD v1
// Table 31 "Ground-Station Pin" column. Link: ICD v1 IF10.0 Table 32.
// =============================================================================
#pragma once

#include <stdint.h>

#define GS_FW_VERSION "4.1.0"
#define GS_BOARD_NAME "NodeMCU-32S"

#if defined(BOARD_CRANE_S3)
// ---------------------------------------------------------------------------
// CROSS-CHECK BUILD (env crane_rf24_check): this same RF24 firmware running on
// the ESP32-S3 crane board, with the crane's ICD Table 31 wiring. Used to tell
// "crane hardware" apart from "crane ESP-IDF firmware". It listens as the crane
// (pipe 1 'REII2') and prints every frame it receives.
// ---------------------------------------------------------------------------
#undef GS_BOARD_NAME
#define GS_BOARD_NAME "ESP32-S3 crane (RF24 cross-check)"
static const uint8_t GS_PIN_CE = 10;    // J7.10 (if CE is on J3.3 / MCP GPA0, strap it to J1.8 instead)
static const uint8_t GS_PIN_CSN = 17;   // J1.1 SPI_xCS
static const int8_t GS_PIN_SCK = 12;    // J1.3
static const int8_t GS_PIN_MISO = 13;   // J1.4
static const int8_t GS_PIN_MOSI = 11;   // J1.2
static const uint8_t CRANE_PWR_EN = 47; // +3.3VP rail (J1.8) enable
static const uint8_t CRANE_ADC_CS = 14; // ADS1220 CS, held high
static const uint8_t CRANE_MMC_CS = 2;  // SD CS, held high
static const uint8_t CRANE_MCP_RST = 5; // MCP23017 /RESET, held released
#else
// nRF24L01+ on the NodeMCU-32S (VSPI)
static const uint8_t GS_PIN_CE = 4;     // nRF pin 3
static const uint8_t GS_PIN_CSN = 5;    // nRF pin 4, VSPI CS0
static const int8_t GS_PIN_SCK = 18;    // nRF pin 5
static const int8_t GS_PIN_MISO = 19;   // nRF pin 7
static const int8_t GS_PIN_MOSI = 23;   // nRF pin 6
#endif
static const uint32_t GS_SPI_HZ = 4000000UL;   // ICD: <= 4 MHz

// Addresses (same byte order as the crane, prints as 0x3149494552 / 0x3249494552)
static const uint8_t GS_ADDR_GS[5] = {'R', 'E', 'I', 'I', '1'};     // ground station listens here
static const uint8_t GS_ADDR_CRANE[5] = {'R', 'E', 'I', 'I', '2'};  // crane listens here

// Normal Crane Control Mode ("APP")
static const uint8_t GS_APP_CHANNEL = 77;
static const uint8_t GS_APP_ARD_STEPS = 1;    // 500 us
static const uint8_t GS_APP_ARC = 5;
// Low-Speed Range Test Mode ("CTRL")
static const uint8_t GS_CTRL_CHANNEL = 80;
static const uint8_t GS_CTRL_ARD_STEPS = 5;   // 1500 us
static const uint8_t GS_CTRL_ARC = 15;

// Boot defaults (bench): PA LOW under 2 m (ICD), ICD profile (dynamic payloads
// + 32-byte ACK-payload telemetry). smoke_test.py sets both ends explicitly.
static const uint8_t GS_DEFAULT_PA = 1;          // 0 MIN, 1 LOW, 2 HIGH, 3 MAX
static const bool GS_DEFAULT_PROFILE_ICD = true;

static const uint32_t GS_BAUD = 115200;          // ICD IF-E1.0
