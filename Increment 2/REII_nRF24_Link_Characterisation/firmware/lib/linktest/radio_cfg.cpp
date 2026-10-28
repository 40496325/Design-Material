#include "radio_cfg.h"

#include <SPI.h>
#include <string.h>

#include "board_pins.h"

#if defined(ESP32)
#include <WiFi.h>
#include <esp_bt.h>
#endif
#if defined(__AVR__)
#include <printf.h>
#endif

namespace lt {

RF24 radio(PIN_NRF_CE, PIN_NRF_CSN, NRF_SPI_HZ);
bool g_isPlus = false;

static const uint8_t* s_mine = nullptr;
static const uint8_t* s_peer = nullptr;

// ARD = 1500 us (5 * 250 us + 250 us) is valid for a 32-byte ACK payload at every rate incl. 250 kbps.
static const uint8_t RETRY_DELAY_STEPS = 5;
static const uint8_t RETRY_COUNT = 15;

bool radioBegin(const uint8_t* mine, const uint8_t* peer) {
  s_mine = mine;
  s_peer = peer;
#if defined(ESP32)
  SPI.begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
#else
  SPI.begin();
#endif
#if defined(__AVR__)
  printf_begin();
#endif
  if (!radio.begin(&SPI)) {
    Serial.println(F("radio.begin() FAILED - check 3V3, CE, CSN, SCK, MOSI, MISO"));
    return false;
  }
  g_isPlus = radio.isPVariant();
  radio.setAddressWidth(5);
  radio.setCRCLength(RF24_CRC_16);
  radio.setPayloadSize(PAYLOAD_SIZE);
  radio.disableDynamicPayloads();
  radio.setRetries(RETRY_DELAY_STEPS, RETRY_COUNT);
  radio.openReadingPipe(1, s_mine);
  radio.openWritingPipe(s_peer);
  applyControlConfig();
  Serial.print(F("radio.begin() OK, isPVariant="));
  Serial.println(g_isPlus ? 1 : 0);
  return true;
}

void applyControlConfig() {
  radio.stopListening();
  radio.setDataRate(g_isPlus ? RF24_250KBPS : RF24_1MBPS);
  radio.setChannel(CTRL_CHANNEL);
  radio.setPALevel(RF24_PA_MAX);
  radio.setAutoAck(true);
  radio.setRetries(RETRY_DELAY_STEPS, RETRY_COUNT);
  radio.flush_rx();
  radio.flush_tx();
  radio.startListening();
}

void applyTestConfig(const TestParams& p) {
  radio.stopListening();
  radio.setDataRate(toRf24Rate(p.rate));
  radio.setChannel(p.channel);  // also resets PLOS_CNT
  radio.setPALevel(toRf24Pa(p.pa));
  radio.setAutoAck(p.mode == MODE_ACK);
  radio.setRetries(RETRY_DELAY_STEPS, RETRY_COUNT);
  radio.flush_rx();
  radio.flush_tx();
}

bool rateSupported(uint8_t rate) {
  if (rate == RATE_250K) return g_isPlus;
  return rate == RATE_1M || rate == RATE_2M;
}

rf24_datarate_e toRf24Rate(uint8_t rate) {
  switch (rate) {
    case RATE_250K: return RF24_250KBPS;
    case RATE_2M: return RF24_2MBPS;
    default: return RF24_1MBPS;
  }
}

rf24_pa_dbm_e toRf24Pa(uint8_t pa) {
  switch (pa) {
    case PA_MIN: return RF24_PA_MIN;
    case PA_LOW: return RF24_PA_LOW;
    case PA_HIGH: return RF24_PA_HIGH;
    default: return RF24_PA_MAX;
  }
}

uint16_t rateKbps(uint8_t rate) {
  switch (rate) {
    case RATE_250K: return 250;
    case RATE_2M: return 2000;
    default: return 1000;
  }
}

const char* paName(uint8_t pa) {
  switch (pa) {
    case PA_MIN: return "MIN";
    case PA_LOW: return "LOW";
    case PA_HIGH: return "HIGH";
    default: return "MAX";
  }
}

const char* modeName(uint8_t mode) { return mode == MODE_ACK ? "ACK" : "NOACK"; }

int parseRate(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "250")) return RATE_250K;
  if (!strcmp(s, "1000") || !strcmp(s, "1M")) return RATE_1M;
  if (!strcmp(s, "2000") || !strcmp(s, "2M")) return RATE_2M;
  return -1;
}

int parsePa(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "MIN")) return PA_MIN;
  if (!strcmp(s, "LOW")) return PA_LOW;
  if (!strcmp(s, "HIGH")) return PA_HIGH;
  if (!strcmp(s, "MAX")) return PA_MAX;
  return -1;
}

int parseMode(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "NOACK")) return MODE_NOACK;
  if (!strcmp(s, "ACK")) return MODE_ACK;
  return -1;
}

bool sendWithRetry(const void* buf, uint32_t timeout_ms) {
  radio.stopListening();
  bool ok = false;
  uint32_t t0 = millis();
  do {
    ok = radio.write(buf, PAYLOAD_SIZE);
    if (!ok) delay(20);
  } while (!ok && millis() - t0 < timeout_ms);
  radio.startListening();
  return ok;
}

void disableWifiBt() {
#if defined(ESP32)
  WiFi.mode(WIFI_OFF);
  btStop();
#endif
}

void serialBegin() {
  Serial.begin(115200);
  // Native-USB boards (ESP32-C3 CDC) drop everything printed before the host
  // opens the port; give it a moment, but never stall a power-bank-fed node.
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < USB_WAIT_MS) delay(10);
  delay(300);
}

bool readSerialLine(char* buf, size_t len) {
  static size_t pos = 0;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      buf[pos] = '\0';
      pos = 0;
      return true;
    }
    if (pos < len - 1) buf[pos++] = c;
  }
  return false;
}

void printBanner(const char* role) {
  Serial.println();
  Serial.print(F("# REII nRF24 linktest v" LINKTEST_VERSION " role="));
  Serial.print(role);
  Serial.print(F(" board=" BOARD_NAME " ctrl_ch="));
  Serial.println(CTRL_CHANNEL);
}

}  // namespace lt
