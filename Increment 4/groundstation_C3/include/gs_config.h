// =============================================================================
// gs_config.h - ground station pins and link settings
// =============================================================================
// groundstation_C3: ESP32-C3 build of the Increment 4 ground station.
// Link settings (addresses, channels, ARC/ARD, PA, profile, baud) are copied
// unchanged from groundstation_PIO v4.1.0 (ICD v1 IF10.0 Table 32). Only the
// board block is new. The NodeMCU and crane cross-check blocks are kept so the
// same source still builds for those boards.
// =============================================================================
#pragma once

#include <stdint.h>

#define GS_FW_VERSION "4.1.0-c3"

#if defined(BOARD_ESP32C3)
// ---------------------------------------------------------------------------
// ESP32-C3 ground station (spare for the NodeMCU-32S).
// Same pins as the Increment 2 C3 transmitter (docs/02_wiring.md, board_pins.h),
// which ran this exact PA+LNA module at 250k / 1M / 2M.
// Avoid: GPIO2/8/9 (strapping; 8 = LED and 9 = BOOT on most boards),
//        GPIO18/19 (native USB D-/D+), GPIO12-17 (module flash), GPIO20/21 (UART0).
// ---------------------------------------------------------------------------
#define GS_BOARD_NAME "ESP32-C3"
static const uint8_t GS_PIN_CE = 10;    // nRF pin 3
static const uint8_t GS_PIN_CSN = 7;    // nRF pin 4, FSPICS0
static const int8_t GS_PIN_SCK = 4;     // nRF pin 5, FSPICLK
static const int8_t GS_PIN_MISO = 5;    // nRF pin 7, FSPIQ
static const int8_t GS_PIN_MOSI = 6;    // nRF pin 6, FSPID
#define GS_PIN_HINT "check 3V3, CE GPIO10, CSN GPIO7, SCK GPIO4, MISO GPIO5, MOSI GPIO6"
// Native USB-CDC drops output printed before the PC opens the port: wait up to
// this long for a host at boot. The dashboard also asks for the banner again
// with a bare Enter, so nothing depends on this wait.
static const uint32_t GS_USB_HOST_WAIT_MS = 3000;

#elif defined(BOARD_CRANE_S3)
// ---------------------------------------------------------------------------
// CROSS-CHECK BUILD: this RF24 firmware on the ESP32-S3 crane board.
// Kept only so the source matches groundstation_PIO; no env in this folder.
// ---------------------------------------------------------------------------
#define GS_BOARD_NAME "ESP32-S3 crane (RF24 cross-check)"
static const uint8_t GS_PIN_CE = 10;    // J7.10
static const uint8_t GS_PIN_CSN = 17;   // J1.1 SPI_xCS
static const int8_t GS_PIN_SCK = 12;    // J1.3
static const int8_t GS_PIN_MISO = 13;   // J1.4
static const int8_t GS_PIN_MOSI = 11;   // J1.2
static const uint8_t CRANE_PWR_EN = 47; // +3.3VP rail (J1.8) enable
static const uint8_t CRANE_ADC_CS = 14; // ADS1220 CS, held high
static const uint8_t CRANE_MMC_CS = 2;  // SD CS, held high
static const uint8_t CRANE_MCP_RST = 5; // MCP23017 /RESET, held released
#define GS_PIN_HINT "check +3.3VP, CE J7.10, CSN GPIO17, SCK 12, MISO 13, MOSI 11"

#else
// nRF24L01+ on the NodeMCU-32S (VSPI) - same as groundstation_PIO
#define GS_BOARD_NAME "NodeMCU-32S"
static const uint8_t GS_PIN_CE = 4;     // nRF pin 3
static const uint8_t GS_PIN_CSN = 5;    // nRF pin 4, VSPI CS0
static const int8_t GS_PIN_SCK = 18;    // nRF pin 5
static const int8_t GS_PIN_MISO = 19;   // nRF pin 7
static const int8_t GS_PIN_MOSI = 23;   // nRF pin 6
#define GS_PIN_HINT "check 3V3, CE GPIO4, CSN GPIO5, SCK 18, MISO 19, MOSI 23"
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

static const uint32_t GS_BAUD = 115200;          // ICD IF-E1.0 (ignored by native USB-CDC)
