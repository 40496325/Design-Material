// ROLE_SCANNER (ESP32 only): 126-channel nRF24 carrier-detect sweep + WiFi AP scan.
//
// Serial commands:
//   SCAN [sweeps]      default 200 sweeps (~25 s). Runs once automatically at boot.
//   WIFI               WiFi AP scan only
//
// Output (parsed by tools/channel_analysis.py):
//   RPD,<ch>,<hits>,<sweeps>          x126
//   WIFI,<ssid>,<bssid>,<wifi_ch>,<rssi_dbm>
//   END
#if defined(ROLE_SCANNER)
#include <Arduino.h>
#include <string.h>

#include "protocol.h"
#include "radio_cfg.h"
#include "roles.h"

#if !defined(ESP32)
#error "ROLE_SCANNER needs an ESP32 board (WiFi scan)"
#endif
#include <WiFi.h>

namespace lt {

static const uint8_t NUM_CHANNELS = 126;
static const uint16_t DEFAULT_SWEEPS = 200;
static const uint16_t DWELL_US = 200;  // > 128 us needed before CD/RPD is valid (NRF24L01.PDF 6.1.3)

static char s_line[96];
static uint16_t s_hits[NUM_CHANNELS];

static void rpdSweep(uint16_t sweeps) {
  memset(s_hits, 0, sizeof(s_hits));
  radio.stopListening();
  radio.setAutoAck(false);
  radio.setDataRate(RF24_1MBPS);
  radio.setPALevel(RF24_PA_MIN);  // we never transmit here; keeps the PA quiet
  for (uint16_t s = 0; s < sweeps; s++) {
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
      radio.setChannel(ch);
      radio.startListening();
      delayMicroseconds(DWELL_US);
      radio.stopListening();
      const bool hit = g_isPlus ? radio.testRPD() : radio.testCarrier();
      if (hit) s_hits[ch]++;
      radio.flush_rx();
    }
    yield();
  }
  for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
    Serial.print(F("RPD,")); Serial.print(ch); Serial.print(',');
    Serial.print(s_hits[ch]); Serial.print(','); Serial.println(sweeps);
  }
}

static void wifiScan() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  const int16_t n = WiFi.scanNetworks(false, true);
  for (int16_t i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    ssid.replace(',', ';');
    if (ssid.length() == 0) ssid = "<hidden>";
    Serial.print(F("WIFI,")); Serial.print(ssid); Serial.print(',');
    Serial.print(WiFi.BSSIDstr(i)); Serial.print(',');
    Serial.print(WiFi.channel(i)); Serial.print(',');
    Serial.println(WiFi.RSSI(i));
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

static void fullScan(uint16_t sweeps) {
  Serial.print(F("SCAN_START,sweeps=")); Serial.println(sweeps);
  rpdSweep(sweeps);
  wifiScan();
  Serial.println(F("END"));
}

static void handleLine(char* line) {
  char* tok = strtok(line, " ");
  if (!tok) {
    printBanner("SCANNER");
    Serial.println(F("READY"));
    return;
  }
  if (!strcmp(tok, "SCAN")) {
    const char* sw = strtok(nullptr, " ");
    long sweeps = sw ? atol(sw) : DEFAULT_SWEEPS;
    if (sweeps < 1 || sweeps > 60000) sweeps = DEFAULT_SWEEPS;
    fullScan((uint16_t)sweeps);
  } else if (!strcmp(tok, "WIFI")) {
    wifiScan();
    Serial.println(F("END"));
  } else if (!strcmp(tok, "INFO")) {
    radio.printPrettyDetails();
  } else {
    Serial.println(F("ERR,unknown command"));
  }
}

void roleSetup() {
  serialBegin();
  disableWifiBt();
  printBanner("SCANNER");
  if (!radioBegin(ADDR_RX_NODE, ADDR_TX_NODE)) {
    for (;;) { delay(1000); Serial.println(F("radio.begin() FAILED")); }
  }
  Serial.println(F("READY"));
  delay(2000);
  fullScan(DEFAULT_SWEEPS);
}

void roleLoop() {
  if (readSerialLine(s_line, sizeof(s_line))) handleLine(s_line);
}

}  // namespace lt
#endif  // ROLE_SCANNER
