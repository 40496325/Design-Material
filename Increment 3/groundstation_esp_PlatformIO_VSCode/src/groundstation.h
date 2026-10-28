#pragma once
#include <Arduino.h>
#include <RF24.h>

// REII ESP groundstation — NodeMCU-32S pin map + RF baseline.
// Pin map source: project_sources/02_increment_2/rf_characterisation/docs/02_wiring.md
// + firmware/lib/linktest/board_pins.h (BOARD_NODEMCU32S). Test-hardware pins reused
// for the groundstation dongle itself (D-08); never copy into the ESP32-S3 controller.

namespace gs {

static const uint8_t PIN_NRF_CE = 4;
static const uint8_t PIN_NRF_CSN = 5;  // VSPI CS0
static const int8_t PIN_NRF_SCK = 18;  // VSPI CLK
static const int8_t PIN_NRF_MISO = 19;  // VSPI Q
static const int8_t PIN_NRF_MOSI = 23;  // VSPI D

// Dupont/breadboard: keep SPI slow. 10 MHz only on a soldered board.
static const uint32_t NRF_SPI_HZ = 4000000UL;

// Application baseline (D-00a, bench-accepted, rail to confirm).
static const uint8_t APP_CHANNEL = 77;  // 2477 MHz
static const rf24_datarate_e APP_RATE = RF24_2MBPS;
static const rf24_pa_dbm_e APP_PA = RF24_PA_MAX;
static const uint8_t APP_ARC = 5;
static const uint16_t APP_ARD_US = 500;
static const uint8_t APP_ADDR_W = 5;

// Control-link fallback (D-00c, reserved).
static const uint8_t CTRL_CHANNEL = 80;  // 2480 MHz
static const uint8_t CTRL_ARC = 15;
static const uint16_t CTRL_ARD_US = 1500;

static const uint8_t PAYLOAD_SIZE = 32;

// Node addresses (kept from the bench rig so the existing TX peer still answers PING).
static const uint8_t ADDR_GS[5] = {'R', 'E', 'I', 'I', '1'};
static const uint8_t ADDR_PEER[5] = {'R', 'E', 'I', 'I', '2'};

extern RF24 radio;
extern bool g_isPlus;

bool radioBegin();
void applyAppConfig();
void applyControlConfig();

}  // namespace gs
