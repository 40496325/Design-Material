#include <Arduino.h>
#include <string.h>

#include <WiFi.h>
#include <esp_bt.h>

#include "groundstation.h"

// REII ESP groundstation dongle — NodeMCU-32S + nRF24L01+ PA+LNA.
// Serial (USB, 115200, newline terminated):
//   INFO            dump nRF24 registers (printPrettyDetails)
//   PING            control-link round trip vs peer (PONG,ok,ms)
//   APP             switch to application baseline ch77/2M (PROVISIONAL link config)
//   CTRL            back to control-link fallback ch80
//   CMD <hex32B>    send 32-byte downlink on app config, report ACK + ACK-payload (hex)
//   ECHO <text>     USB-CDC echo (bring-up step 1, isolates USB before RF)
//   HELP            command list
//
// Bring-up order (D-05 adapted for ESP): ECHO -> INFO -> PING -> APP smoke -> CMD loopback.
// Wiring: 3.3V only, 10uF+100nF at module, antenna before power, SPI <= 4 MHz Dupont,
// PA_LOW below ~2 m (LNA saturation). See docs/04_rf_baseline.md.

namespace {

char s_line[160];

void printBanner() {
  Serial.println();
  Serial.print(F("# REII groundstation v"));
  Serial.print(GROUNDSTATION_VERSION);
  Serial.print(F(" board=NodeMCU-32S app_ch="));
  Serial.print(gs::APP_CHANNEL);
  Serial.print(F(" ctrl_ch="));
  Serial.println(gs::CTRL_CHANNEL);
}

bool hexNibble(char c, uint8_t& out) {
  if (c >= '0' && c <= '9') { out = (uint8_t)(c - '0'); return true; }
  if (c >= 'a' && c <= 'f') { out = (uint8_t)(c - 'a' + 10); return true; }
  if (c >= 'A' && c <= 'F') { out = (uint8_t)(c - 'A' + 10); return true; }
  return false;
}

bool parseHex32(const char* s, uint8_t* buf) {
  if (!s) return false;
  size_t len = strlen(s);
  if (len != 64) return false;
  for (size_t i = 0; i < 32; i++) {
    uint8_t hi, lo;
    if (!hexNibble(s[2 * i], hi) || !hexNibble(s[2 * i + 1], lo)) return false;
    buf[i] = (uint8_t)((hi << 4) | lo);
  }
  return true;
}

void printHex(const uint8_t* buf, size_t n) {
  for (size_t i = 0; i < n; i++) {
    if (buf[i] < 16) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
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

void handleLine(char* line) {
  char* tok = strtok(line, " ");
  if (!tok) {
    printBanner();
    Serial.println(F("READY"));
    return;
  }
  if (!strcmp(tok, "HELP")) {
    Serial.println(F("CMDS: INFO PING APP CTRL CMD<hex64> ECHO<text> HELP"));
  } else if (!strcmp(tok, "ECHO")) {
    char* rest = strtok(nullptr, "");
    Serial.print(F("ECHO,"));
    Serial.println(rest ? rest : "");
  } else if (!strcmp(tok, "INFO")) {
    gs::radio.printPrettyDetails();
  } else if (!strcmp(tok, "CTRL")) {
     gs::applyControlConfig();
    Serial.println(F("OK,ctrl_ch=80"));
  } else if (!strcmp(tok, "APP")) {
    gs::applyAppConfig();
    Serial.println(F("OK,app_ch=77,2M,PA_MAX,ARC5,ARD500"));
  } else if (!strcmp(tok, "PING")) {
    uint8_t ping[gs::PAYLOAD_SIZE];
    memset(ping, 0, sizeof(ping));
    ping[0] = 0xA5;  // matches bench MAGIC_CMD
    ping[1] = 0x01;  // CMD_PING
    gs::radio.stopListening();
    const uint32_t t0 = millis();
    const bool ok = gs::radio.write(ping, sizeof(ping));
    gs::radio.startListening();
    Serial.print(F("PONG,"));
    Serial.print(ok ? 1 : 0);
    Serial.print(',');
    Serial.println(millis() - t0);
  } else if (!strcmp(tok, "CMD")) {
    char* hex = strtok(nullptr, " ");
    uint8_t frame[gs::PAYLOAD_SIZE];
    if (!parseHex32(hex ? hex : "", frame)) {
      Serial.println(F("ERR,usage: CMD <64 hex chars = 32 bytes>"));
      return;
    }
    gs::radio.stopListening();
    const bool ok = gs::radio.write(frame, sizeof(frame));
    uint8_t ack[gs::PAYLOAD_SIZE];
    bool hasAck = false;
    if (ok && gs::radio.isAckPayloadAvailable()) {
      gs::radio.read(&ack, sizeof(ack));
      hasAck = true;
    }
    gs::radio.startListening();
    Serial.print(F("ACK,"));
    Serial.print(ok ? 1 : 0);
    Serial.print(',');
    if (hasAck) {
      Serial.print(F("payload="));
      printHex(ack, sizeof(ack));
      Serial.println();
    } else {
      Serial.println(F("payload=none"));
    }
  } else {
    Serial.println(F("ERR,unknown command (try HELP)"));
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  const uint32_t t0 = millis();
  while (!Serial && millis() - t0 < 3000) delay(10);
  delay(300);
  WiFi.mode(WIFI_OFF);
  btStop();
  printBanner();
  if (!gs::radioBegin()) {
    for (;;) { delay(1000); Serial.println(F("radio.begin() FAILED")); }
  }
  gs::radio.printPrettyDetails();
  Serial.println(F("READY"));
}

void loop() {
  if (readSerialLine(s_line, sizeof(s_line))) handleLine(s_line);
  // Drain stale RX frames so they never confuse the next command.
  while (gs::radio.available()) {
    uint8_t junk[gs::PAYLOAD_SIZE];
    gs::radio.read(junk, sizeof(junk));
  }
}