#pragma once
#include <stdint.h>

// Over-the-air frames. All exactly PAYLOAD_SIZE bytes (static payload length).
// Byte order: little-endian on every MCU we use (AVR, Xtensa, RISC-V).

namespace lt {

static const uint8_t PAYLOAD_SIZE = 32;

// Each node has one address it listens on; the peer writes to it.
static const uint8_t ADDR_RX_NODE[5] = {'R', 'E', 'I', 'I', '1'};  // RX (ground-station stand-in)
static const uint8_t ADDR_TX_NODE[5] = {'R', 'E', 'I', 'I', '2'};  // TX (mobile)

// Control-link configuration: most robust settings we have, above the WiFi band.
static const uint8_t CTRL_CHANNEL = 80;   // 2480 MHz: in-band, above WiFi 1/6/11, 2 MHz clear of the 2483.5 MHz edge

enum Magic : uint8_t {
  MAGIC_CMD = 0xA5,     // RX -> TX
  MAGIC_DATA = 0x5A,    // TX -> RX, test burst
  MAGIC_REPORT = 0xC3,  // TX -> RX, after burst
};

enum CmdType : uint8_t {
  CMD_PING = 1,
  CMD_TEST = 2,
};

enum Mode : uint8_t {
  MODE_NOACK = 0,
  MODE_ACK = 1,
};

enum RateId : uint8_t {
  RATE_250K = 0,
  RATE_1M = 1,
  RATE_2M = 2,
};

enum PaId : uint8_t {
  PA_MIN = 0,
  PA_LOW = 1,
  PA_HIGH = 2,
  PA_MAX = 3,
};

struct __attribute__((packed)) TestParams {
  uint8_t mode;         // Mode
  uint8_t rate;         // RateId
  uint8_t channel;      // RF_CH 0..125
  uint8_t pa;           // PaId
  uint16_t n;           // packets in burst
  uint16_t spacing_us;  // gap between packets (NOACK); ACK mode sends back-to-back
  uint16_t run_id;
};

struct __attribute__((packed)) CmdFrame {
  uint8_t magic;  // MAGIC_CMD
  uint8_t type;   // CmdType
  TestParams p;   // 10 bytes
  uint8_t pad[PAYLOAD_SIZE - 2 - sizeof(TestParams)];
};

struct __attribute__((packed)) DataFrame {
  uint8_t magic;  // MAGIC_DATA
  uint8_t mode;
  uint16_t run_id;
  uint16_t seq;    // 0..n-1
  uint32_t tx_ms;  // millis() at TX
  uint8_t pad[PAYLOAD_SIZE - 10];
};

struct __attribute__((packed)) ReportFrame {
  uint8_t magic;  // MAGIC_REPORT
  uint8_t status;
  uint16_t run_id;
  uint16_t sent;
  uint16_t acked;   // ACK mode only
  uint16_t failed;  // ACK mode only
  uint32_t retries_total;
  uint8_t retries_max;
  uint32_t duration_ms;
  uint8_t pad[PAYLOAD_SIZE - 19];
};

static_assert(sizeof(CmdFrame) == PAYLOAD_SIZE, "CmdFrame size");
static_assert(sizeof(DataFrame) == PAYLOAD_SIZE, "DataFrame size");
static_assert(sizeof(ReportFrame) == PAYLOAD_SIZE, "ReportFrame size");

enum ReportStatus : uint8_t {
  ST_OK = 0,
  ST_ABORTED = 1,  // TX gave up after ABORT_CONSECUTIVE_FAILS in ACK mode
};

// Limits / timing (see docs/03_test_procedure.md "Running it")
static const uint16_t N_MAX = 2000;                   // RX bitmap = 250 bytes, fits the Nano too
static const uint16_t DEFAULT_SPACING_US = 2000;      // NOACK packet spacing
static const uint32_t CTRL_RETRY_MS = 2000;           // master keeps trying to reach the TX this long
static const uint32_t SWITCH_GUARD_MS = 60;           // TX waits this after a command before test config
static const uint32_t LAST_PKT_GRACE_MS = 200;        // RX keeps listening this long after seq n-1
static const uint32_t ACK_SILENCE_MS = 5000;          // RX gives up after this silence (ACK mode)
static const uint32_t ACK_PER_PKT_WORST_MS = 50;      // 16 attempts x (1.5 ms ARD + 1.3 ms frame)
static const uint16_t ABORT_CONSECUTIVE_FAILS = 100;  // ~4.5 s of total loss in ACK mode
static const uint32_t REPORT_DELAY_MS = 150;          // TX waits this after burst before the report
static const uint32_t REPORT_RETRY_MS = 6000;         // TX retries the report this long
static const uint32_t REPORT_TIMEOUT_MS = 5000;       // RX waits this for the report

}  // namespace lt
