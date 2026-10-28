#pragma once
#include <stdint.h>

// Pin maps for the boards in ../datasheets. GPIO numbers, not header positions.
// Keep in sync with docs/02_wiring.md.

#if defined(BOARD_NODEMCU32S)
static const uint8_t PIN_NRF_CE = 4;
static const uint8_t PIN_NRF_CSN = 5;  // VSPI CS0
static const int8_t PIN_NRF_SCK = 18;  // VSPI
static const int8_t PIN_NRF_MISO = 19;
static const int8_t PIN_NRF_MOSI = 23;
#define BOARD_NAME "NodeMCU-32S"

#elif defined(BOARD_ESP32C3)
static const uint8_t PIN_NRF_CE = 10;
static const uint8_t PIN_NRF_CSN = 7;  // FSPICS0
static const int8_t PIN_NRF_SCK = 4;   // FSPICLK
static const int8_t PIN_NRF_MISO = 5;  // FSPIQ
static const int8_t PIN_NRF_MOSI = 6;  // FSPID
#define BOARD_NAME "ESP32-C3"

#elif defined(BOARD_NANO)
static const uint8_t PIN_NRF_CE = 9;
static const uint8_t PIN_NRF_CSN = 10;
static const int8_t PIN_NRF_SCK = -1;  // fixed hardware SPI: D13/D12/D11
static const int8_t PIN_NRF_MISO = -1;
static const int8_t PIN_NRF_MOSI = -1;
#define BOARD_NAME "Arduino Nano"

#else
#error "Define one of BOARD_NODEMCU32S / BOARD_ESP32C3 / BOARD_NANO in platformio.ini"
#endif

// Breadboard + Dupont leads: keep SPI slow. Raise to 10 MHz on a soldered board.
static const uint32_t NRF_SPI_HZ = 4000000UL;
