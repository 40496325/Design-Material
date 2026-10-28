// ROLE_TX: mobile transmitter. Waits on the control link for TEST commands from the RX,
// runs the burst, reports back. Also has a BEACON mode for bring-up with RX's LISTEN.
//
// Serial commands (optional, the node is normally headless on a power bank):
//   INFO
//   BEACON <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> [NOACK|ACK]   10 pkt/s until any key
//   CTRL
#if defined(ROLE_TX)
#include <Arduino.h>
#include <string.h>

#include "protocol.h"
#include "radio_cfg.h"
#include "roles.h"

namespace lt {

static char s_line[96];

static void runBurst(const TestParams& p) {
  ReportFrame rpt;
  memset(&rpt, 0, sizeof(rpt));
  rpt.magic = MAGIC_REPORT;
  rpt.status = ST_OK;
  rpt.run_id = p.run_id;

  if (!rateSupported(p.rate)) {
    // Should not happen (RX checks too); report zero packets.
    delay(REPORT_DELAY_MS);
    sendWithRetry(&rpt, REPORT_RETRY_MS);
    return;
  }

  delay(SWITCH_GUARD_MS);
  applyTestConfig(p);  // leaves the radio in standby (not listening)

  DataFrame f;
  memset(&f, 0, sizeof(f));
  f.magic = MAGIC_DATA;
  f.mode = p.mode;
  f.run_id = p.run_id;

  uint16_t consecutiveFails = 0;
  const uint32_t t0 = millis();
  for (uint16_t seq = 0; seq < p.n; seq++) {
    f.seq = seq;
    f.tx_ms = millis();
    const uint32_t slot = micros();
    const bool ok = radio.write(&f, sizeof(f));
    rpt.sent++;
    if (p.mode == MODE_ACK) {
      const uint8_t arc = radio.getARC();
      rpt.retries_total += arc;
      if (arc > rpt.retries_max) rpt.retries_max = arc;
      if (ok) {
        rpt.acked++;
        consecutiveFails = 0;
      } else {
        rpt.failed++;
        if (++consecutiveFails >= ABORT_CONSECUTIVE_FAILS) {
          rpt.status = ST_ABORTED;
          break;
        }
      }
    } else {
      while (micros() - slot < p.spacing_us) { /* pace the burst */ }
    }
    yield();
  }
  rpt.duration_ms = millis() - t0;

  delay(REPORT_DELAY_MS);
  applyControlConfig();
  const bool delivered = sendWithRetry(&rpt, REPORT_RETRY_MS);

  Serial.print(F("BURST,"));
  Serial.print(p.run_id); Serial.print(',');
  Serial.print(modeName(p.mode)); Serial.print(',');
  Serial.print(rateKbps(p.rate)); Serial.print(',');
  Serial.print(p.channel); Serial.print(',');
  Serial.print(paName(p.pa)); Serial.print(',');
  Serial.print(rpt.sent); Serial.print(',');
  Serial.print(rpt.acked); Serial.print(',');
  Serial.print(rpt.failed); Serial.print(',');
  Serial.print(rpt.retries_total); Serial.print(',');
  Serial.print(rpt.retries_max); Serial.print(',');
  Serial.print(rpt.duration_ms); Serial.print(',');
  Serial.print(rpt.status == ST_ABORTED ? "ABORTED" : "OK"); Serial.print(',');
  Serial.println(delivered ? "REPORTED" : "REPORT_LOST");
}

static void beacon(const TestParams& p) {
  applyTestConfig(p);
  Serial.println(F("BEACON (any key to stop)"));
  DataFrame f;
  memset(&f, 0, sizeof(f));
  f.magic = MAGIC_DATA;
  f.mode = p.mode;
  uint16_t seq = 0, okCount = 0;
  while (!Serial.available()) {
    f.seq = seq++;
    f.tx_ms = millis();
    if (radio.write(&f, sizeof(f))) okCount++;
    if ((seq & 0x0F) == 0) {
      Serial.print(F("BEACON,sent=")); Serial.print(seq);
      Serial.print(F(",ok=")); Serial.println(okCount);
    }
    delay(100);
  }
  while (Serial.available()) Serial.read();
  applyControlConfig();
  Serial.println(F("STOPPED"));
}

static void handleLine(char* line) {
  char* tok = strtok(line, " ");
  if (!tok) {  // bare Enter: re-show who we are (banner may have been missed on USB-CDC)
    printBanner("TX");
    Serial.println(F("READY (waiting for commands from RX)"));
    return;
  }
  if (!strcmp(tok, "INFO")) {
    radio.printPrettyDetails();
  } else if (!strcmp(tok, "CTRL")) {
    applyControlConfig();
    Serial.println(F("OK"));
  } else if (!strcmp(tok, "BEACON")) {
    TestParams p;
    memset(&p, 0, sizeof(p));
    const int rate = parseRate(strtok(nullptr, " "));
    const char* chS = strtok(nullptr, " ");
    const int pa = parsePa(strtok(nullptr, " "));
    const char* modeS = strtok(nullptr, " ");
    if (rate < 0 || !chS || pa < 0) {
      Serial.println(F("ERR,usage: BEACON <250|1000|2000> <ch> <MIN|LOW|HIGH|MAX> [NOACK|ACK]"));
      return;
    }
    p.rate = (uint8_t)rate;
    p.channel = (uint8_t)atol(chS);
    p.pa = (uint8_t)pa;
    p.mode = modeS && parseMode(modeS) == MODE_ACK ? MODE_ACK : MODE_NOACK;
    beacon(p);
  } else {
    Serial.println(F("ERR,unknown command"));
  }
}

void roleSetup() {
  serialBegin();
  disableWifiBt();
  printBanner("TX");
  if (!radioBegin(ADDR_TX_NODE, ADDR_RX_NODE)) {
    for (;;) { delay(1000); Serial.println(F("radio.begin() FAILED")); }
  }
  radio.printPrettyDetails();
  Serial.println(F("READY (waiting for commands from RX)"));
}

void roleLoop() {
  if (readSerialLine(s_line, sizeof(s_line))) handleLine(s_line);
  while (radio.available()) {
    CmdFrame cmd;
    radio.read(&cmd, sizeof(cmd));
    if (cmd.magic != MAGIC_CMD) continue;
    if (cmd.type == CMD_PING) {
      Serial.println(F("PING"));
    } else if (cmd.type == CMD_TEST) {
      Serial.print(F("TEST,")); Serial.print(cmd.p.run_id); Serial.print(',');
      Serial.print(modeName(cmd.p.mode)); Serial.print(',');
      Serial.print(rateKbps(cmd.p.rate)); Serial.print(',');
      Serial.print(cmd.p.channel); Serial.print(',');
      Serial.print(paName(cmd.p.pa)); Serial.print(',');
      Serial.println(cmd.p.n);
      runBurst(cmd.p);
    }
  }
}

}  // namespace lt
#endif  // ROLE_TX
