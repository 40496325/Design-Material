#pragma once
#include <Arduino.h>
#include <RF24.h>

namespace gs {

// NodeMCU-32S VSPI Pin Map
static const uint8_t PIN_NRF_CE = 4;
static const uint8_t PIN_NRF_CSN = 5;  // VSPI CS0
static const int8_t PIN_NRF_SCK = 18;  // VSPI CLK
static const int8_t PIN_NRF_MISO = 19; // VSPI MISO
static const int8_t PIN_NRF_MOSI = 23; // VSPI MOSI

static const uint32_t NRF_SPI_HZ = 4000000UL;

// Application baseline: Channel 77 @ 2 Mbps, PA_MAX, ARC=5, ARD=500us
static const uint8_t APP_CHANNEL = 77;
static const rf24_datarate_e APP_RATE = RF24_2MBPS;
static const rf24_pa_dbm_e APP_PA = RF24_PA_MAX;
static const uint8_t APP_ARC = 5;
static const uint16_t APP_ARD_US = 500;
static const uint8_t APP_ADDR_W = 5;

// Control/Diagnostic baseline: Channel 80 @ 250 kbps
static const uint8_t CTRL_CHANNEL = 80;
static const uint8_t CTRL_ARC = 15;
static const uint16_t CTRL_ARD_US = 1500;

static const uint8_t PAYLOAD_SIZE = 32;

// Groundstation listens on 'REII1', writes to Crane 'REII2'
static const uint8_t ADDR_GS[5]   = {'R', 'E', 'I', 'I', '1'};
static const uint8_t ADDR_PEER[5] = {'R', 'E', 'I', 'I', '2'};

extern RF24 radio;
extern bool g_isPlus;

bool radioBegin();
void applyAppConfig();
void applyControlConfig();
void restoreAppConfig();

}  // namespace gs