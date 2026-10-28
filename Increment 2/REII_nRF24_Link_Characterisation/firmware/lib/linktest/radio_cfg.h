#pragma once
#include <Arduino.h>
#include <RF24.h>

#include "protocol.h"

namespace lt {

extern RF24 radio;
extern bool g_isPlus;  // nRF24L01+ detected (250 kbps + RPD available)

// SPI + radio.begin() + settings common to every role. Prints the boot banner.
// `mine` is the address this node listens on, `peer` the one it writes to.
bool radioBegin(const uint8_t* mine, const uint8_t* peer);

// Most robust configuration, used to exchange commands / reports.
void applyControlConfig();
// Rate / channel / PA / auto-ack for one test burst.
void applyTestConfig(const TestParams& p);

bool rateSupported(uint8_t rate);
rf24_datarate_e toRf24Rate(uint8_t rate);
rf24_pa_dbm_e toRf24Pa(uint8_t pa);
uint16_t rateKbps(uint8_t rate);
const char* paName(uint8_t pa);
const char* modeName(uint8_t mode);
// Parsers for the serial command interface; return -1 on error.
int parseRate(const char* s);  // "250" | "1000" | "2000"
int parsePa(const char* s);    // "MIN" | "LOW" | "HIGH" | "MAX"
int parseMode(const char* s);  // "NOACK" | "ACK"

// stopListening -> write() until it succeeds or timeout -> startListening.
bool sendWithRetry(const void* buf, uint32_t timeout_ms);

// Turn the ESP32's own 2.4 GHz radios off; no-op on AVR.
void disableWifiBt();

// Serial.begin(115200), waiting up to USB_WAIT_MS for a native-USB host to attach.
constexpr uint32_t USB_WAIT_MS = 3000;
void serialBegin();

// Non-blocking serial line reader. Returns true once a full line is in `buf`.
bool readSerialLine(char* buf, size_t len);

void printBanner(const char* role);

}  // namespace lt
