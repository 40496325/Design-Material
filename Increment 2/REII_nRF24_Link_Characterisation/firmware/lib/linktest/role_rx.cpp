// ROLE_RX: ground-station stand-in. PC-tethered test master.
//
// Serial commands (115200, newline terminated):
//   INFO                              dump nRF24 registers
//   PING                              reach the TX over the control link
//   TEST <NOACK|ACK> <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> <n> [spacing_us]
//   LISTEN <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> [NOACK|ACK]   print every packet until any key
//   CTRL                              back to control configuration
//
// Output lines are CSV, prefixed with a keyword, so tools/run_test.py can parse them:
//   RESULT,run_id,mode,rate_kbps,ch,pa,n,tx_sent,rx_unique,dup,ooo,tx_acked,tx_failed,
//          retries_total,retries_max,rpd_hits,rpd_samples,duration_ms,status
#if defined(ROLE_RX)
#include <Arduino.h>
#include <string.h>

#include "protocol.h"
#include "radio_cfg.h"
#include "roles.h"

namespace lt {

static char s_line[96];
static uint8_t s_bitmap[N_MAX / 8];
static uint16_t s_runId = 0;

struct RxStats {
  uint16_t unique = 0, dup = 0, ooo = 0;
  uint32_t rpdHits = 0, rpdSamples = 0;
  int32_t lastSeq = -1;
  bool sawLast = false;
};

static void printResult(const TestParams& p, const RxStats& rx, const ReportFrame* rpt, const char* status) {
  Serial.print(F("RESULT,"));
  Serial.print(p.run_id); Serial.print(',');
  Serial.print(modeName(p.mode)); Serial.print(',');
  Serial.print(rateKbps(p.rate)); Serial.print(',');
  Serial.print(p.channel); Serial.print(',');
  Serial.print(paName(p.pa)); Serial.print(',');
  Serial.print(p.n); Serial.print(',');
  Serial.print(rpt ? rpt->sent : 0); Serial.print(',');
  Serial.print(rx.unique); Serial.print(',');
  Serial.print(rx.dup); Serial.print(',');
  Serial.print(rx.ooo); Serial.print(',');
  Serial.print(rpt ? rpt->acked : 0); Serial.print(',');
  Serial.print(rpt ? rpt->failed : 0); Serial.print(',');
  Serial.print(rpt ? rpt->retries_total : 0); Serial.print(',');
  Serial.print(rpt ? rpt->retries_max : 0); Serial.print(',');
  Serial.print(rx.rpdHits); Serial.print(',');
  Serial.print(rx.rpdSamples); Serial.print(',');
  Serial.print(rpt ? rpt->duration_ms : 0); Serial.print(',');
  Serial.println(status);
}

static void listenBurst(const TestParams& p, RxStats& rx) {
  memset(s_bitmap, 0, sizeof(s_bitmap));
  const uint32_t expectedMs = (uint32_t)p.n * p.spacing_us / 1000UL;
  const uint32_t silenceMs = (p.mode == MODE_ACK) ? ACK_SILENCE_MS : expectedMs + 500;
  const uint32_t hardCapMs = (p.mode == MODE_ACK) ? (uint32_t)p.n * ACK_PER_PKT_WORST_MS + 1000 : expectedMs + 1000;

  applyTestConfig(p);
  radio.startListening();
  const uint32_t t0 = millis();
  uint32_t tLastPkt = t0, tLastRpd = 0, tSawLast = 0;

  for (;;) {
    const uint32_t now = millis();
    while (radio.available()) {
      DataFrame f;
      radio.read(&f, sizeof(f));
      tLastPkt = now;
      if (f.magic != MAGIC_DATA || f.run_id != p.run_id || f.seq >= p.n) continue;
      const uint8_t bit = 1 << (f.seq & 7);
      if (s_bitmap[f.seq >> 3] & bit) {
        rx.dup++;
      } else {
        s_bitmap[f.seq >> 3] |= bit;
        rx.unique++;
        if ((int32_t)f.seq < rx.lastSeq) rx.ooo++;
        rx.lastSeq = f.seq;
      }
      if (f.seq == p.n - 1 && !rx.sawLast) {
        rx.sawLast = true;
        tSawLast = now;
      }
    }
    if (g_isPlus && now != tLastRpd) {  // ~1 kHz RPD sampling
      tLastRpd = now;
      rx.rpdSamples++;
      if (radio.testRPD()) rx.rpdHits++;
    }
    if (rx.sawLast && now - tSawLast >= LAST_PKT_GRACE_MS) break;
    if (now - tLastPkt >= silenceMs) break;
    if (now - t0 >= hardCapMs) break;
    yield();
  }
  applyControlConfig();
}

static bool waitReport(uint16_t runId, ReportFrame& out) {
  const uint32_t t0 = millis();
  while (millis() - t0 < REPORT_TIMEOUT_MS) {
    while (radio.available()) {
      radio.read(&out, sizeof(out));
      if (out.magic == MAGIC_REPORT && out.run_id == runId) return true;
    }
    yield();
  }
  return false;
}

static void runTest(const TestParams& pIn) {
  TestParams p = pIn;
  p.run_id = ++s_runId;
  RxStats rx;

  if (!rateSupported(p.rate)) {
    printResult(p, rx, nullptr, "RATE_UNSUPPORTED");
    return;
  }
  CmdFrame cmd;
  memset(&cmd, 0, sizeof(cmd));
  cmd.magic = MAGIC_CMD;
  cmd.type = CMD_TEST;
  cmd.p = p;
  if (!sendWithRetry(&cmd, CTRL_RETRY_MS)) {
    printResult(p, rx, nullptr, "CTRL_TIMEOUT");
    return;
  }
  // TX waits SWITCH_GUARD_MS before it starts; we switch immediately so we are listening first.
  listenBurst(p, rx);

  ReportFrame rpt;
  if (!waitReport(p.run_id, rpt)) {
    printResult(p, rx, nullptr, "REPORT_TIMEOUT");
    return;
  }
  printResult(p, rx, &rpt, rpt.status == ST_ABORTED ? "TX_ABORTED" : "OK");
}

static void listenForever(const TestParams& p) {
  applyTestConfig(p);
  radio.startListening();
  Serial.println(F("LISTENING (any key to stop)"));
  uint32_t count = 0, t0 = millis();
  while (!Serial.available()) {
    while (radio.available()) {
      DataFrame f;
      radio.read(&f, sizeof(f));
      count++;
      Serial.print(F("PKT,"));
      Serial.print(millis() - t0); Serial.print(',');
      Serial.print(f.magic, HEX); Serial.print(',');
      Serial.print(f.seq); Serial.print(',');
      Serial.print(f.tx_ms); Serial.print(',');
      Serial.println(g_isPlus && radio.testRPD() ? 1 : 0);
    }
    yield();
  }
  while (Serial.available()) Serial.read();
  Serial.print(F("STOPPED, packets=")); Serial.println(count);
  applyControlConfig();
}

static void handleLine(char* line) {
  char* tok = strtok(line, " ");
  if (!tok) {  // bare Enter: re-show who we are (banner may have been missed on USB-CDC)
    printBanner("RX");
    Serial.println(F("READY"));
    return;
  }
  if (!strcmp(tok, "INFO")) {
    radio.printPrettyDetails();
  } else if (!strcmp(tok, "CTRL")) {
    applyControlConfig();
    Serial.println(F("OK"));
  } else if (!strcmp(tok, "PING")) {
    CmdFrame cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.magic = MAGIC_CMD;
    cmd.type = CMD_PING;
    const uint32_t t0 = millis();
    const bool ok = sendWithRetry(&cmd, CTRL_RETRY_MS);
    Serial.print(F("PONG,")); Serial.print(ok ? 1 : 0); Serial.print(','); Serial.println(millis() - t0);
  } else if (!strcmp(tok, "TEST")) {
    TestParams p;
    memset(&p, 0, sizeof(p));
    const int mode = parseMode(strtok(nullptr, " "));
    const int rate = parseRate(strtok(nullptr, " "));
    const char* chS = strtok(nullptr, " ");
    const int pa = parsePa(strtok(nullptr, " "));
    const char* nS = strtok(nullptr, " ");
    const char* spS = strtok(nullptr, " ");
    if (mode < 0 || rate < 0 || !chS || pa < 0 || !nS) {
      Serial.println(F("ERR,usage: TEST <NOACK|ACK> <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> <n> [spacing_us]"));
      return;
    }
    const long ch = atol(chS), n = atol(nS);
    if (ch < 0 || ch > 125 || n < 1 || n > N_MAX) {
      Serial.println(F("ERR,range: ch 0..125, n 1..2000"));
      return;
    }
    p.mode = (uint8_t)mode;
    p.rate = (uint8_t)rate;
    p.channel = (uint8_t)ch;
    p.pa = (uint8_t)pa;
    p.n = (uint16_t)n;
    p.spacing_us = spS ? (uint16_t)atol(spS) : DEFAULT_SPACING_US;
    runTest(p);
  } else if (!strcmp(tok, "LISTEN")) {
    TestParams p;
    memset(&p, 0, sizeof(p));
    const int rate = parseRate(strtok(nullptr, " "));
    const char* chS = strtok(nullptr, " ");
    const int pa = parsePa(strtok(nullptr, " "));
    const char* modeS = strtok(nullptr, " ");
    if (rate < 0 || !chS || pa < 0) {
      Serial.println(F("ERR,usage: LISTEN <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> [NOACK|ACK]"));
      return;
    }
    p.rate = (uint8_t)rate;
    p.channel = (uint8_t)atol(chS);
    p.pa = (uint8_t)pa;
    p.mode = modeS && parseMode(modeS) == MODE_ACK ? MODE_ACK : MODE_NOACK;
    listenForever(p);
  } else {
    Serial.println(F("ERR,unknown command"));
  }
}

void roleSetup() {
  serialBegin();
  disableWifiBt();
  printBanner("RX");
  if (!radioBegin(ADDR_RX_NODE, ADDR_TX_NODE)) {
    for (;;) { delay(1000); Serial.println(F("radio.begin() FAILED")); }
  }
  radio.printPrettyDetails();
  Serial.println(F("READY"));
}

void roleLoop() {
  if (readSerialLine(s_line, sizeof(s_line))) handleLine(s_line);
  // Drain anything unexpected on the control link so stale frames don't confuse the next test.
  while (radio.available()) {
    uint8_t junk[PAYLOAD_SIZE];
    radio.read(junk, sizeof(junk));
  }
}

}  // namespace lt
#endif  // ROLE_RX
