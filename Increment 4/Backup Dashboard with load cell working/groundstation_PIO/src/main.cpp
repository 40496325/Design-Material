// =============================================================================
// REII ground station - NodeMCU-32S + nRF24L01+ (Increment 4)
// =============================================================================
// PC <-> USB serial (115200, IF-E1.0) <-> this board <-> nRF24 (IF10.0) <-> crane
//
// The ground station is the primary transmitter (PTX): it writes 32-byte
// command frames (ICD Table 33) to the crane on 'REII2'; the crane's automatic
// ACK carries the 32-byte telemetry frame back (ICD Table 34) when the ICD
// profile is active.
//
// Serial commands (one per line, case-insensitive):
//   HELP                      list commands
//   ECHO <text>               -> ECHO,<text>                (USB check mode)
//   INFO                      radio register dump + link line
//   APP                       Normal Crane Control Mode: ch77 / 2 Mbps / ARC5 / ARD500
//   CTRL                      Low-Speed Range Test Mode: ch80 / 250 kbps / ARC15 / ARD1500
//   PROFILE <ICD|STATIC>      ICD = dynamic payloads + ACK-payload telemetry, STATIC = fixed 32 B
//   PA <MIN|LOW|HIGH|MAX>     transmit power
//   CH <0-125>                override the channel
//   PING [seq]                one 0xA5/type 1 frame -> PONG,<ack>,<ms>,<arc>,<us>,<seq>
//                             + TLM,<len>,<hex> with the ACK payload (ICD profile)
//   CMD <64 hex chars>        send a raw 32-byte frame (dashboard)
//                             -> ACK,<ack>,payload=<hex>,us=<rtt_us>,arc=<retries>,seq=<byte 2>
//   STATS / CLEAR             counters
//   SCAN [passes]             RPD sweep ch 0-125 -> SCAN,<ch>,<hits>,<passes> ... SCAN_END,...
//   RPD [ms]                  sample RPD on the current channel -> RPD,<hits>,<samples>
//   CARRIER [ms] [ch]         constant carrier at PA_MAX (so the crane's RPD can hear us)
// =============================================================================
#include <Arduino.h>
#include <RF24.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_bt.h>
#include <string.h>

#include "gs_config.h"
#include "icd_frames.h"

static RF24 radio(GS_PIN_CE, GS_PIN_CSN, GS_SPI_HZ);

static bool g_radioOk = false;
static bool g_isPlus = false;
static bool g_profileIcd = GS_DEFAULT_PROFILE_ICD;
static bool g_modeApp = true;
static uint8_t g_pa = GS_DEFAULT_PA;
static uint8_t g_channel = GS_APP_CHANNEL;
static uint8_t g_seq = 0;
static uint32_t g_sent = 0, g_acked = 0, g_failed = 0, g_tlm = 0;

static const char* paName(uint8_t pa) {
  static const char* n[4] = {"MIN", "LOW", "HIGH", "MAX"};
  return n[pa & 3];
}

static rf24_pa_dbm_e toRf24Pa(uint8_t pa) {
  switch (pa & 3) {
    case 0: return RF24_PA_MIN;
    case 1: return RF24_PA_LOW;
    case 2: return RF24_PA_HIGH;
    default: return RF24_PA_MAX;
  }
}

static int parsePa(const char* s) {
  if (!s) return -1;
  if (!strcmp(s, "MIN") || !strcmp(s, "0")) return 0;
  if (!strcmp(s, "LOW") || !strcmp(s, "1")) return 1;
  if (!strcmp(s, "HIGH") || !strcmp(s, "2")) return 2;
  if (!strcmp(s, "MAX") || !strcmp(s, "3")) return 3;
  return -1;
}

static void printHex(const uint8_t* buf, uint8_t len) {
  static const char* hex = "0123456789ABCDEF";
  for (uint8_t i = 0; i < len; i++) {
    Serial.print(hex[buf[i] >> 4]);
    Serial.print(hex[buf[i] & 0x0F]);
  }
}

// Full link configuration. Leaves the radio in TX standby (not listening):
// the ground station only ever talks first, the crane answers in the ACK.
static void applyConfig() {
  radio.ce(LOW);
  radio.setAddressWidth(5);
  radio.setCRCLength(RF24_CRC_16);
  radio.setAutoAck(true);
  if (g_profileIcd) {
    radio.enableDynamicPayloads();
    radio.enableAckPayload();
  } else {
    radio.disableAckPayload();
    radio.disableDynamicPayloads();
    radio.setPayloadSize(ICD_FRAME_BYTES);
  }
  if (g_modeApp) {
    radio.setDataRate(RF24_2MBPS);
    radio.setRetries(GS_APP_ARD_STEPS, GS_APP_ARC);
  } else {
    radio.setDataRate(g_isPlus ? RF24_250KBPS : RF24_1MBPS);
    radio.setRetries(GS_CTRL_ARD_STEPS, GS_CTRL_ARC);
  }
  radio.setChannel(g_channel);
  radio.setPALevel(toRf24Pa(g_pa));
#if defined(BOARD_CRANE_S3)
  // Cross-check build: behave like the crane - primary receiver on pipe 1 'REII2'.
  radio.openReadingPipe(1, GS_ADDR_CRANE);
  radio.stopListening(GS_ADDR_GS);
  radio.flush_rx();
  radio.flush_tx();
  radio.startListening();
#else
  radio.openReadingPipe(1, GS_ADDR_GS);
  radio.stopListening(GS_ADDR_CRANE);   // TX_ADDR + RX_ADDR_P0 = 'REII2' (for the ACK)
  radio.flush_rx();
  radio.flush_tx();
#endif
}

static void printLinkLine() {
  Serial.print(F("GS,LINK,mode="));
  Serial.print(g_modeApp ? F("APP") : F("CTRL"));
  Serial.print(F(",ch="));
  Serial.print(g_channel);
  Serial.print(F(",rate="));
  Serial.print(g_modeApp ? F("2M") : F("250K"));
  Serial.print(F(",pa="));
  Serial.print(paName(g_pa));
  Serial.print(F(",arc="));
  Serial.print(g_modeApp ? GS_APP_ARC : GS_CTRL_ARC);
  Serial.print(F(",ard_us="));
  Serial.print(((g_modeApp ? GS_APP_ARD_STEPS : GS_CTRL_ARD_STEPS) + 1) * 250);
  Serial.print(F(",profile="));
  Serial.print(g_profileIcd ? F("ICD") : F("STATIC"));
  Serial.print(F(",payload="));
  Serial.print(g_profileIcd ? F("dyn32+ackpl32") : F("static32"));
  Serial.print(F(",tx_to=REII2,radio="));
  Serial.println(g_radioOk ? F("OK") : F("NOT_RESPONDING"));
}

static void printModeOk() {
  // Same first fields as the Increment 3 dashboard expects: OK,app_ch=77,2M,PA_x,ARC5,ARD500
  if (g_modeApp) {
    Serial.print(F("OK,app_ch="));
    Serial.print(g_channel);
    Serial.print(F(",2M,PA_"));
    Serial.print(paName(g_pa));
    Serial.println(F(",ARC5,ARD500"));
  } else {
    Serial.print(F("OK,ctrl_ch="));
    Serial.print(g_channel);
    Serial.print(g_isPlus ? F(",250K,PA_") : F(",1M,PA_"));
    Serial.print(paName(g_pa));
    Serial.println(F(",ARC15,ARD1500"));
  }
}

static void printBanner() {
  Serial.println();
  Serial.print(F("# REII groundstation v" GS_FW_VERSION " board=" GS_BOARD_NAME " app_ch="));
  Serial.print(GS_APP_CHANNEL);
  Serial.print(F(" ctrl_ch="));
  Serial.print(GS_CTRL_CHANNEL);
  Serial.println(F(" (Increment 4)"));
}

// Sends one 32-byte frame and collects the ACK payload (if any).
// Returns true if the crane's auto-ACK arrived.
static bool sendFrame(const uint8_t* frame, uint32_t* rtt_us, uint8_t* arc, uint8_t* ack, uint8_t* ackLen) {
  *ackLen = 0;
  radio.flush_rx();   // drop any stale ACK payload so the one we read belongs to this frame
  const uint32_t t0 = micros();
  const bool ok = radio.write(frame, ICD_FRAME_BYTES);
  *rtt_us = micros() - t0;
  *arc = radio.getARC();
  g_sent++;
  if (ok) {
    g_acked++;
    if (g_profileIcd && radio.available()) {
      uint8_t len = radio.getDynamicPayloadSize();
      if (len >= 1 && len <= 32) {
        radio.read(ack, len);
        *ackLen = len;
        g_tlm++;
      } else {
        radio.flush_rx();   // corrupt width
      }
    }
  } else {
    g_failed++;
  }
  return ok;
}

static void cmdPing(const char* arg) {
  icd_downlink_t f;
  memset(&f, 0, sizeof(f));
  f.magic = ICD_MAGIC_CMD;
  f.msg_type = ICD_MSG_PING;
  f.sequence = arg ? (uint8_t)atoi(arg) : g_seq++;
  uint32_t us;
  uint8_t arc, ack[32], ackLen;
  const bool ok = sendFrame(reinterpret_cast<const uint8_t*>(&f), &us, &arc, ack, &ackLen);
  Serial.print(F("PONG,"));
  Serial.print(ok ? 1 : 0);
  Serial.print(',');
  Serial.print(us / 1000);
  Serial.print(',');
  Serial.print(arc);
  Serial.print(',');
  Serial.print(us);
  Serial.print(',');
  Serial.println(f.sequence);
  if (g_profileIcd) {
    Serial.print(F("TLM,"));
    Serial.print(ackLen);
    Serial.print(',');
    printHex(ack, ackLen);
    Serial.println();
  }
}

static int hexNibble(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return -1;
}

static void cmdRaw(const char* hex) {
  uint8_t frame[ICD_FRAME_BYTES];
  if (!hex || strlen(hex) != 2 * ICD_FRAME_BYTES) {
    Serial.println(F("ERR,usage: CMD <64 hex chars = 32 bytes>"));
    return;
  }
  for (int i = 0; i < ICD_FRAME_BYTES; i++) {
    const int hi = hexNibble(hex[2 * i]), lo = hexNibble(hex[2 * i + 1]);
    if (hi < 0 || lo < 0) {
      Serial.println(F("ERR,bad hex"));
      return;
    }
    frame[i] = (uint8_t)((hi << 4) | lo);
  }
  uint32_t us;
  uint8_t arc, ack[32], ackLen;
  const bool ok = sendFrame(frame, &us, &arc, ack, &ackLen);
  // ACK,<ok>,payload=<hex>  (unchanged first fields)  + ,us=<rtt>,arc=<retries>,seq=<frame byte 2>
  Serial.print(F("ACK,"));
  Serial.print(ok ? 1 : 0);
  Serial.print(F(",payload="));
  printHex(ack, ackLen);
  Serial.print(F(",us="));
  Serial.print(us);
  Serial.print(F(",arc="));
  Serial.print(arc);
  Serial.print(F(",seq="));
  Serial.println(frame[2]);
}

static void cmdScan(int passes) {
  static uint16_t hits[126];
  memset(hits, 0, sizeof(hits));
  for (int p = 0; p < passes; p++) {
    for (uint8_t ch = 0; ch <= 125; ch++) {
      radio.setChannel(ch);
      radio.startListening();
      delayMicroseconds(200);
      if (radio.testRPD()) hits[ch]++;
      radio.stopListening();
    }
    yield();
  }
  uint32_t total = 0;
  uint8_t busiest = 0;
  for (uint8_t ch = 0; ch <= 125; ch++) {
    Serial.print(F("SCAN,"));
    Serial.print(ch);
    Serial.print(',');
    Serial.print(hits[ch]);
    Serial.print(',');
    Serial.println(passes);
    total += hits[ch];
    if (hits[ch] > hits[busiest]) busiest = ch;
  }
  Serial.print(F("SCAN_END,total_hits="));
  Serial.print(total);
  Serial.print(F(",busiest="));
  Serial.println(busiest);
  applyConfig();
}

static void cmdRpd(uint32_t ms) {
  uint32_t hits = 0, samples = 0;
  radio.startListening();
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    radio.ce(LOW);   // re-enter RX so RPD is a fresh reading each sample
    radio.ce(HIGH);
    delayMicroseconds(200);
    samples++;
    if (radio.testRPD()) hits++;
    if ((samples & 0x3F) == 0) yield();
  }
  radio.stopListening();
  Serial.print(F("RPD,"));
  Serial.print(hits);
  Serial.print(',');
  Serial.println(samples);
  applyConfig();
}

static void cmdCarrier(uint32_t ms, uint8_t ch) {
  Serial.print(F("GS,CARRIER,ON,ch="));
  Serial.print(ch);
  Serial.print(F(",ms="));
  Serial.println(ms);
  radio.startConstCarrier(RF24_PA_MAX, ch);
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) delay(5);
  radio.stopConstCarrier();
  radio.powerUp();
  Serial.println(F("GS,CARRIER,OFF"));
  applyConfig();   // startConstCarrier changes CRC / auto-ACK / retries / TX_ADDR
}

static void printStats() {
  Serial.print(F("GS,STATS,sent="));
  Serial.print(g_sent);
  Serial.print(F(",acked="));
  Serial.print(g_acked);
  Serial.print(F(",failed="));
  Serial.print(g_failed);
  Serial.print(F(",tlm="));
  Serial.print(g_tlm);
  Serial.print(F(",next_seq="));
  Serial.println(g_seq);
}

static void printHelp() {
  Serial.println(F("GS,HELP,HELP ECHO <text> INFO APP CTRL PROFILE <ICD|STATIC> PA <MIN|LOW|HIGH|MAX> CH <0-125> "
                   "PING [seq] CMD <hex64> STATS CLEAR SCAN [passes] RPD [ms] CARRIER [ms] [ch]"));
  Serial.println(F("OK,HELP"));
}

static void handleLine(char* line) {
  // ECHO keeps its text exactly as typed
  if (!strncasecmp(line, "ECHO", 4) && (line[4] == ' ' || line[4] == '\0')) {
    Serial.print(F("ECHO,"));
    Serial.println(line[4] ? line + 5 : "");
    return;
  }
  for (char* p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
  char* tok = strtok(line, " \t");
  if (!tok) {   // bare Enter: re-show who we are
    printBanner();
    printLinkLine();
    Serial.println(F("READY"));
    return;
  }
  char* a1 = strtok(nullptr, " \t");
  char* a2 = strtok(nullptr, " \t");

  if (!strcmp(tok, "HELP")) {
    printHelp();
    return;
  }
  if (!g_radioOk && strcmp(tok, "INFO")) {
    Serial.println(F("ERR,radio not responding - check 3V3, CE GPIO4, CSN GPIO5, SCK 18, MISO 19, MOSI 23"));
    return;
  }
  if (!strcmp(tok, "INFO")) {
    printBanner();
    if (g_radioOk) radio.printPrettyDetails();
    printLinkLine();
    printStats();
    Serial.println(F("OK,INFO"));
  } else if (!strcmp(tok, "APP") || !strcmp(tok, "CTRL")) {
    if (!strcmp(tok, "CTRL") && !g_isPlus) {
      Serial.println(F("ERR,250 kbps needs an nRF24L01+"));
      return;
    }
    g_modeApp = !strcmp(tok, "APP");
    g_channel = g_modeApp ? GS_APP_CHANNEL : GS_CTRL_CHANNEL;
    applyConfig();
    printModeOk();
  } else if (!strcmp(tok, "PROFILE")) {
    if (a1 && (!strcmp(a1, "ICD") || !strcmp(a1, "STATIC"))) {
      g_profileIcd = !strcmp(a1, "ICD");
      applyConfig();
      Serial.print(F("OK,PROFILE,"));
      Serial.println(g_profileIcd ? F("ICD") : F("STATIC"));
    } else {
      Serial.println(F("ERR,usage: PROFILE <ICD|STATIC>"));
    }
  } else if (!strcmp(tok, "PA")) {
    const int pa = parsePa(a1);
    if (pa < 0) {
      Serial.println(F("ERR,usage: PA <MIN|LOW|HIGH|MAX>"));
      return;
    }
    g_pa = (uint8_t)pa;
    applyConfig();
    Serial.print(F("OK,PA,"));
    Serial.println(paName(g_pa));
  } else if (!strcmp(tok, "CH")) {
    const int ch = a1 ? atoi(a1) : -1;
    if (ch < 0 || ch > 125) {
      Serial.println(F("ERR,usage: CH <0-125>"));
      return;
    }
    g_channel = (uint8_t)ch;
    applyConfig();
    Serial.print(F("OK,CH,"));
    Serial.println(g_channel);
  } else if (!strcmp(tok, "PING")) {
    cmdPing(a1);
  } else if (!strcmp(tok, "CMD")) {
    cmdRaw(a1);
  } else if (!strcmp(tok, "STATS")) {
    printStats();
    Serial.println(F("OK,STATS"));
  } else if (!strcmp(tok, "CLEAR")) {
    g_sent = g_acked = g_failed = g_tlm = 0;
    g_seq = 0;
    Serial.println(F("OK,CLEAR"));
  } else if (!strcmp(tok, "SCAN")) {
    int passes = a1 ? atoi(a1) : 20;
    if (passes < 1 || passes > 1000) passes = 20;
    cmdScan(passes);
    Serial.println(F("OK,SCAN"));
  } else if (!strcmp(tok, "RPD")) {
    long ms = a1 ? atol(a1) : 2000;
    if (ms < 10 || ms > 60000) ms = 2000;
    cmdRpd((uint32_t)ms);
    Serial.println(F("OK,RPD"));
  } else if (!strcmp(tok, "CARRIER")) {
    long ms = a1 ? atol(a1) : 5000;
    long ch = a2 ? atol(a2) : g_channel;
    if (ms < 10 || ms > 60000) ms = 5000;
    if (ch < 0 || ch > 125) ch = g_channel;
    cmdCarrier((uint32_t)ms, (uint8_t)ch);
    Serial.println(F("OK,CARRIER"));
  } else {
    Serial.print(F("ERR,unknown command '"));
    Serial.print(tok);
    Serial.println(F("' (try HELP)"));
  }
}

static bool readLine(char* buf, size_t len) {
  static size_t pos = 0;
  while (Serial.available()) {
    const char c = (char)Serial.read();
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

void setup() {
  Serial.begin(GS_BAUD);
  delay(300);
  // The ESP32's own 2.4 GHz radios stay off: they would share the band with the nRF.
  WiFi.mode(WIFI_OFF);
  btStop();

#if defined(BOARD_CRANE_S3)
  // Crane board: +3.3VP rail on, every other SPI chip select high, MCP23017 out of reset.
  pinMode(CRANE_PWR_EN, OUTPUT);
  digitalWrite(CRANE_PWR_EN, HIGH);
  pinMode(CRANE_ADC_CS, OUTPUT);
  digitalWrite(CRANE_ADC_CS, HIGH);
  pinMode(CRANE_MMC_CS, OUTPUT);
  digitalWrite(CRANE_MMC_CS, HIGH);
  pinMode(CRANE_MCP_RST, OUTPUT);
  digitalWrite(CRANE_MCP_RST, HIGH);
  delay(150);   // rail settle + nRF power-on reset
#endif
  printBanner();
  SPI.begin(GS_PIN_SCK, GS_PIN_MISO, GS_PIN_MOSI, GS_PIN_CSN);
  g_radioOk = radio.begin(&SPI);
  if (!g_radioOk) {
    Serial.println(F("radio.begin() FAILED - check 3V3, CE GPIO4, CSN GPIO5, SCK 18, MISO 19, MOSI 23"));
  } else {
    g_isPlus = radio.isPVariant();
    Serial.print(F("radio.begin() OK, isPVariant="));
    Serial.println(g_isPlus ? 1 : 0);
    applyConfig();
    radio.printPrettyDetails();
  }
  printLinkLine();
  if (g_radioOk) printModeOk();
  Serial.println(F("READY"));
}

void loop() {
  static char line[96];
  if (readLine(line, sizeof(line))) handleLine(line);
#if defined(BOARD_CRANE_S3)
  static uint32_t rxCount = 0, lastHb = 0;
  uint8_t pipe;
  while (g_radioOk && radio.available(&pipe)) {
    uint8_t buf[32];
    uint8_t len = g_profileIcd ? radio.getDynamicPayloadSize() : ICD_FRAME_BYTES;
    if (len < 1 || len > 32) {
      radio.flush_rx();
      break;
    }
    radio.read(buf, len);
    rxCount++;
    // Same format as the ESP-IDF crane firmware, so smoke_test.py can match rounds.
    Serial.print(F("RIG,RX,"));
    Serial.print(rxCount);
    Serial.print(',');
    Serial.print(buf[0] == ICD_MAGIC_CMD ? buf[1] : 0);
    Serial.print(',');
    Serial.print(buf[2]);
    Serial.print(',');
    Serial.print(len);
    Serial.print(',');
    Serial.print(pipe);
    Serial.println(F(",0"));
  }
  if (millis() - lastHb > 5000) {
    lastHb = millis();
    Serial.print(F("RIG,HB,rf24_check=1,frames="));
    Serial.print(rxCount);
    Serial.print(F(",ch="));
    Serial.print(g_channel);
    Serial.print(F(",profile="));
    Serial.println(g_profileIcd ? F("ICD") : F("STATIC"));
  }
#else
  if (g_radioOk && radio.available()) radio.flush_rx();   // nothing is expected outside a PING/CMD
#endif
}
