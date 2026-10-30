#include "groundstation.h"
#include <SPI.h>
#include <WiFi.h>
#include <esp_bt.h>

namespace gs {

RF24 radio(PIN_NRF_CE, PIN_NRF_CSN, NRF_SPI_HZ);
bool g_isPlus = false;

static void radioCommon() {
  radio.setAddressWidth(APP_ADDR_W);
  radio.setCRCLength(RF24_CRC_16);
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.openReadingPipe(1, ADDR_GS);
  radio.openWritingPipe(ADDR_PEER);
  radio.flush_rx();
  radio.flush_tx();
}

bool radioBegin() {
  SPI.begin(PIN_NRF_SCK, PIN_NRF_MISO, PIN_NRF_MOSI, PIN_NRF_CSN);
  if (!radio.begin(&SPI)) {
    Serial.println(F("radio.begin() FAILED - check 3V3, CE, CSN, SCK, MOSI, MISO"));
    return false;
  }
  g_isPlus = radio.isPVariant();
  radioCommon();
  applyAppConfig();
  Serial.print(F("radio.begin() OK, isPVariant="));
  Serial.println(g_isPlus ? 1 : 0);
  return true;
}

void applyAppConfig() {
  radio.stopListening();
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setDataRate(APP_RATE);
  radio.setChannel(APP_CHANNEL);
  radio.setPALevel(APP_PA);
  radio.setAutoAck(true);
  radio.setRetries(1, APP_ARC); // ARD 500 us (step=1), ARC 5
  radio.flush_rx();
  radio.flush_tx();
  radio.startListening();
}

// DIAG: full restore after startConstCarrier() (it disables CRC/auto-ACK/retries).
void restoreAppConfig() {
  radio.powerUp();
  delay(5);
  radioCommon();
  applyAppConfig();
}

void applyControlConfig() {
  radio.stopListening();
  radio.enableDynamicPayloads();
  radio.enableAckPayload();
  radio.setDataRate(g_isPlus ? RF24_250KBPS : RF24_1MBPS);
  radio.setChannel(CTRL_CHANNEL);
  radio.setPALevel(RF24_PA_MAX);
  radio.setAutoAck(true);
  radio.setRetries(5, CTRL_ARC); // ARD 1500 us (step=5), ARC 15
  radio.flush_rx();
  radio.flush_tx();
  radio.startListening();
}

}  // namespace gs