// =============================================================================
// rf_link.c - crane side of the REII wireless link (ICD v1 IF10.0)
// =============================================================================
//
// Machine-readable console lines (parsed by tools/smoke_test.py):
//   RIG,BOOT,...                       once, after the radio is configured
//   RIG,REGS,CONFIG=0F,...             register read-back
//   RIG,ADDR,...                       pipe addresses
//   RIG,RX,<n>,<type>,<seq>,<len>,<pipe>,<dup>   one line per received frame
//   RIG,HB,...                         heartbeat every RIG_HEARTBEAT_MS
//   RIG,SCAN,<ch>,<hits>,<passes> / RIG,SCAN_END,<total>,<busiest_ch>
//   RIG,RPD,<hits>,<samples>
//   RIG,CARRIER,ON|OFF,...
//   RIG,WARN,... / RIG,ERR,...
//   OK,<cmd>... / ERR,<msg>            replies to console commands
// =============================================================================

#include "rf_link.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "icd_frames.h"
#include "nrf24.h"
#include "rig_config.h"
#include "rig_telemetry.h"
#include "loadcell.h"
#include "status_led.h"

static const char *TAG = "rf_link";

#define LINE_MAX_LEN 96

typedef enum { MODE_APP = 0, MODE_CTRL = 1 } link_mode_t;

typedef struct {
    // configuration
    bool profile_icd;
    link_mode_t mode;
    int pa;
    int channel;
    bool log_frames;
    // radio state
    bool radio_ok;
    esp_err_t probe_err;
    uint8_t probe_setup_aw;
    int last_cfg_errors;
    uint8_t expect_config;
    // counters
    uint32_t frames, pings, moves, bad, dups, reinit, ackpl_loaded;
    uint32_t hb_frames_prev;
    int last_seq;
    int64_t last_rx_us;
    bool link_ok;
    icd_downlink_t last_cmd;
    icd_uplink_t tlm;
} link_state_t;

static nrf24_t s_radio;
static link_state_t s;
static QueueHandle_t s_lines;
static mcp23017_handle_t *s_mcp;
__attribute__((unused)) static uint8_t s_mcp_olat_a;   // shadow of OLATA (CE on GPA0)

static const uint8_t ADDR_CRANE[5] = RIG_ADDR_CRANE;
static const uint8_t ADDR_GS[5] = RIG_ADDR_GS;

static inline uint32_t now_ms(void) { return (uint32_t)(esp_timer_get_time() / 1000); }

// -----------------------------------------------------------------------------
// CE: J3.3 (MCP23017 GPA0) and/or J7.10 (GPIO10), see rig_config.h
// -----------------------------------------------------------------------------
static void ce_write(void *ctx, int level)
{
    (void)ctx;
#if RIG_CE_PATHS & RIG_CE_PATH_GPIO_J7_10
    gpio_set_level(RIG_NRF_CE_GPIO, level);
#endif
#if RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3
    if (s_mcp) {
        uint8_t bit = (uint8_t)(1u << RIG_NRF_CE_MCP_BIT);
        s_mcp_olat_a = level ? (uint8_t)(s_mcp_olat_a | bit) : (uint8_t)(s_mcp_olat_a & ~bit);
        if (mcp23017_write_port(s_mcp, MCP23017_PORT_A, s_mcp_olat_a) != ESP_OK) {
            printf("RIG,WARN,MCP23017 write failed - CE on J3.3 may be stale\n");
        }
    }
#endif
}

// Reads back the CE level actually present on each path (-1 = path not used / unreadable).
static void ce_readback(int *gpio_level, int *mcp_level)
{
    *gpio_level = -1;
    *mcp_level = -1;
#if RIG_CE_PATHS & RIG_CE_PATH_GPIO_J7_10
    *gpio_level = gpio_get_level(RIG_NRF_CE_GPIO);
#endif
#if RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3
    uint8_t v;
    if (s_mcp && mcp23017_read_port(s_mcp, MCP23017_PORT_A, &v) == ESP_OK) {
        *mcp_level = (v >> RIG_NRF_CE_MCP_BIT) & 1;
    }
#endif
}

static const char *ce_paths_name(void)
{
#if (RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3) && (RIG_CE_PATHS & RIG_CE_PATH_GPIO_J7_10)
    return s_mcp ? "J3.3(MCP GPA0)+J7.10(GPIO10)" : "J7.10(GPIO10) only - MCP23017 missing";
#elif RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3
    return s_mcp ? "J3.3(MCP GPA0)" : "J3.3(MCP GPA0) - MCP23017 MISSING, CE NOT DRIVEN";
#else
    return "J7.10(GPIO10)";
#endif
}

// -----------------------------------------------------------------------------
// Radio configuration
// -----------------------------------------------------------------------------
static uint8_t rf_setup_value(link_mode_t mode, int pa)
{
    uint8_t dr = (mode == MODE_APP) ? NRF_RF_DR_HIGH : NRF_RF_DR_LOW;  // 2 Mbps : 250 kbps
    // Bit 0 = LNA_HCURR: obsolete on a genuine nRF24L01+, but clone chips use it.
    // Set it exactly like RF24::setPALevel() on the ground station does.
    return (uint8_t)(dr | NRF_RF_PWR(pa) | 0x01);
}

static uint8_t setup_retr_value(link_mode_t mode)
{
    return (mode == MODE_APP) ? (uint8_t)((RIG_APP_ARD_STEPS << 4) | RIG_APP_ARC)
                              : (uint8_t)((RIG_CTRL_ARD_STEPS << 4) | RIG_CTRL_ARC);
}

static const char *mode_name(link_mode_t m) { return m == MODE_APP ? "APP" : "CTRL"; }
static const char *rate_name(link_mode_t m) { return m == MODE_APP ? "2M" : "250K"; }

static void build_telemetry(void)
{
    // ICD Table 34 frame from the active telemetry source (rig_telemetry.c).
    rig_tlm_build(&s.tlm, (uint8_t)(s.last_seq < 0 ? 0 : s.last_seq), s.link_ok);
}

// Keeps exactly one telemetry frame waiting in the TX FIFO for the next ACK.
static void load_ack_payload(void)
{
    if (!s.profile_icd || !s.radio_ok) return;
    build_telemetry();
    if (!(nrf24_read_reg(&s_radio, NRF_REG_FIFO_STATUS) & NRF_FIFO_TX_EMPTY)) {
        nrf24_flush_tx(&s_radio);   // drop a stale one so the reply is never more than 1 frame old
    }
    nrf24_write_ack_payload(&s_radio, 1, (const uint8_t *)&s.tlm, sizeof(s.tlm));
    s.ackpl_loaded++;
}

typedef struct {
    uint8_t reg;
    uint8_t expect;
    uint8_t mask;
    const char *name;
} reg_check_t;

static void print_regs(void)
{
    printf("RIG,REGS,CONFIG=%02X,EN_AA=%02X,EN_RXADDR=%02X,SETUP_AW=%02X,SETUP_RETR=%02X,RF_CH=%u,"
           "RF_SETUP=%02X,STATUS=%02X,FIFO=%02X,RX_PW_P1=%u,DYNPD=%02X,FEATURE=%02X\n",
           nrf24_read_reg(&s_radio, NRF_REG_CONFIG), nrf24_read_reg(&s_radio, NRF_REG_EN_AA),
           nrf24_read_reg(&s_radio, NRF_REG_EN_RXADDR), nrf24_read_reg(&s_radio, NRF_REG_SETUP_AW),
           nrf24_read_reg(&s_radio, NRF_REG_SETUP_RETR), nrf24_read_reg(&s_radio, NRF_REG_RF_CH),
           nrf24_read_reg(&s_radio, NRF_REG_RF_SETUP), nrf24_status(&s_radio),
           nrf24_read_reg(&s_radio, NRF_REG_FIFO_STATUS), nrf24_read_reg(&s_radio, NRF_REG_RX_PW_P1),
           nrf24_read_reg(&s_radio, NRF_REG_DYNPD), nrf24_read_reg(&s_radio, NRF_REG_FEATURE));
}

static void print_addr(void)
{
    uint8_t p0[5], p1[5], tx[5];
    nrf24_read_regn(&s_radio, NRF_REG_RX_ADDR_P0, p0, 5);
    nrf24_read_regn(&s_radio, NRF_REG_RX_ADDR_P1, p1, 5);
    nrf24_read_regn(&s_radio, NRF_REG_TX_ADDR, tx, 5);
    // Printed MSB first, the way RF24's printDetails() shows them on the ground station.
    printf("RIG,ADDR,RX_ADDR_P1=0x%02X%02X%02X%02X%02X('%c%c%c%c%c'),TX_ADDR=0x%02X%02X%02X%02X%02X,"
           "RX_ADDR_P0=0x%02X%02X%02X%02X%02X\n",
           p1[4], p1[3], p1[2], p1[1], p1[0], p1[0], p1[1], p1[2], p1[3], p1[4],
           tx[4], tx[3], tx[2], tx[1], tx[0], p0[4], p0[3], p0[2], p0[1], p0[0]);
}

// Writes the complete link configuration and reads every register back.
// Returns the number of registers that did not read back as written.
static int apply_config(void)
{
    nrf24_ce(&s_radio, 0);
    const uint8_t cfg_base = NRF_CONFIG_EN_CRC | NRF_CONFIG_CRCO | NRF_CONFIG_PWR_UP;  // CRC-16, powered
    nrf24_write_reg(&s_radio, NRF_REG_CONFIG, cfg_base);
    vTaskDelay(pdMS_TO_TICKS(3));   // Tpd2stby 1.5 ms (4.5 ms worst case with external clock)

    const uint8_t retr = setup_retr_value(s.mode);
    const uint8_t rfs = rf_setup_value(s.mode, s.pa);
    const uint8_t feature = s.profile_icd ? (NRF_FEATURE_EN_DPL | NRF_FEATURE_EN_ACK_PAY) : 0x00;
    const uint8_t dynpd = s.profile_icd ? 0x03 : 0x00;

    nrf24_write_reg(&s_radio, NRF_REG_SETUP_AW, 0x03);          // 5-byte addresses
    nrf24_write_reg(&s_radio, NRF_REG_SETUP_RETR, retr);
    nrf24_write_reg(&s_radio, NRF_REG_RF_CH, (uint8_t)s.channel);
    nrf24_write_reg(&s_radio, NRF_REG_RF_SETUP, rfs);
    nrf24_write_regn(&s_radio, NRF_REG_RX_ADDR_P1, ADDR_CRANE, 5);
    nrf24_write_regn(&s_radio, NRF_REG_TX_ADDR, ADDR_GS, 5);
    nrf24_write_regn(&s_radio, NRF_REG_RX_ADDR_P0, ADDR_GS, 5);  // for ACKs if the crane ever transmits
    nrf24_write_reg(&s_radio, NRF_REG_EN_RXADDR, 0x02);         // listen on pipe 1 only
    nrf24_write_reg(&s_radio, NRF_REG_EN_AA, 0x03);             // auto-ACK on pipes 0 and 1
    nrf24_write_reg(&s_radio, NRF_REG_RX_PW_P0, RIG_PAYLOAD_BYTES);
    nrf24_write_reg(&s_radio, NRF_REG_RX_PW_P1, RIG_PAYLOAD_BYTES);
    nrf24_write_reg(&s_radio, NRF_REG_FEATURE, feature);
    nrf24_write_reg(&s_radio, NRF_REG_DYNPD, dynpd);
    nrf24_flush_rx(&s_radio);
    nrf24_flush_tx(&s_radio);
    nrf24_clear_irqs(&s_radio);

    s.expect_config = cfg_base | NRF_CONFIG_PRIM_RX;
    nrf24_write_reg(&s_radio, NRF_REG_CONFIG, s.expect_config);

    const reg_check_t checks[] = {
        {NRF_REG_CONFIG, s.expect_config, 0x7F, "CONFIG"},
        {NRF_REG_EN_AA, 0x03, 0x3F, "EN_AA"},
        {NRF_REG_EN_RXADDR, 0x02, 0x3F, "EN_RXADDR"},
        {NRF_REG_SETUP_AW, 0x03, 0x03, "SETUP_AW"},
        {NRF_REG_SETUP_RETR, retr, 0xFF, "SETUP_RETR"},
        {NRF_REG_RF_CH, (uint8_t)s.channel, 0x7F, "RF_CH"},
        {NRF_REG_RF_SETUP, rfs, 0x2F, "RF_SETUP"},
        {NRF_REG_RX_PW_P1, RIG_PAYLOAD_BYTES, 0x3F, "RX_PW_P1"},
        {NRF_REG_FEATURE, feature, 0x07, "FEATURE"},
        {NRF_REG_DYNPD, dynpd, 0x3F, "DYNPD"},
    };
    int errors = 0;
    for (size_t i = 0; i < sizeof(checks) / sizeof(checks[0]); i++) {
        uint8_t got = nrf24_read_reg(&s_radio, checks[i].reg);
        if ((got & checks[i].mask) != (checks[i].expect & checks[i].mask)) {
            printf("RIG,ERR,read-back %s=%02X expected %02X\n", checks[i].name, got, checks[i].expect);
            errors++;
        }
    }
    uint8_t a[5];
    nrf24_read_regn(&s_radio, NRF_REG_RX_ADDR_P1, a, 5);
    if (memcmp(a, ADDR_CRANE, 5) != 0) {
        printf("RIG,ERR,read-back RX_ADDR_P1 does not match 'REII2'\n");
        errors++;
    }
    nrf24_read_regn(&s_radio, NRF_REG_TX_ADDR, a, 5);
    if (memcmp(a, ADDR_GS, 5) != 0) {
        printf("RIG,ERR,read-back TX_ADDR does not match 'REII1'\n");
        errors++;
    }

    nrf24_ce(&s_radio, 1);           // ICD: CE HIGH so the crane radio listens continuously
    esp_rom_delay_us(200);           // Tstby2a 130 us
    s.last_cfg_errors = errors;
    load_ack_payload();
    return errors;
}

static void print_link_line(const char *tag)
{
    int ce_gpio, ce_mcp;
    ce_readback(&ce_gpio, &ce_mcp);
    printf("RIG,%s,profile=%s,mode=%s,ch=%d,rate=%s,pa=%s(%ddBm),arc=%d,ard_us=%d,crc=16,aw=5,payload=%s,"
           "listen=REII2,ce_paths=%s,ce_j7_10=%d,ce_j3_3=%d,cfg_errors=%d\n",
           tag, s.profile_icd ? "ICD" : "STATIC", mode_name(s.mode), s.channel, rate_name(s.mode),
           nrf24_pa_name(s.pa), nrf24_pa_dbm(s.pa), s.mode == MODE_APP ? RIG_APP_ARC : RIG_CTRL_ARC,
           s.mode == MODE_APP ? (RIG_APP_ARD_STEPS + 1) * 250 : (RIG_CTRL_ARD_STEPS + 1) * 250,
           s.profile_icd ? "dyn32+ackpl32" : "static32", ce_paths_name(), ce_gpio, ce_mcp,
           s.last_cfg_errors);
}

// -----------------------------------------------------------------------------
// Receive path
// -----------------------------------------------------------------------------
static void handle_frame(const uint8_t *buf, int len, int pipe)
{
    s.frames++;
    s.last_rx_us = esp_timer_get_time();
    if (!s.link_ok) {
        s.link_ok = true;
        status_led_set_rx_mode(true);
    }
    status_led_notify_rx();

    if (len < 3 || buf[0] != ICD_MAGIC_CMD) {
        s.bad++;
        if (s.log_frames) {
            printf("RIG,RX,%" PRIu32 ",BAD,%02X,%d,%d,0\n", s.frames, buf[0], len, pipe);
        }
        load_ack_payload();
        return;
    }
    const uint8_t type = buf[1];
    const uint8_t seq = buf[2];
    bool cmd_changed = false;
    const int dup = (s.last_seq == seq) ? 1 : 0;
    if (dup) s.dups++;
    s.last_seq = seq;

    if (type == ICD_MSG_PING) {
        s.pings++;
    } else if (type == ICD_MSG_MOVE) {
        s.moves++;
        icd_downlink_t prev = s.last_cmd;
        memset(&s.last_cmd, 0, sizeof(s.last_cmd));
        memcpy(&s.last_cmd, buf, len < (int)sizeof(s.last_cmd) ? (size_t)len : sizeof(s.last_cmd));
        cmd_changed = memcmp(&prev.mode_flags, &s.last_cmd.mode_flags, 5) != 0;   // bytes 3..7
        // No actuation in this firmware: the command feeds the telemetry model only.
        rig_tlm_on_move(&s.last_cmd, s.last_rx_us);
    } else {
        s.bad++;
    }
    if (s.log_frames) {
        printf("RIG,RX,%" PRIu32 ",%u,%u,%d,%d,%d\n", s.frames, type, seq, len, pipe, dup);
        if (type == ICD_MSG_MOVE && cmd_changed) {   // only when the operator changed something
            printf("RIG,CMD,seq=%u,flags=%02X,trolley=%d,hoist=%d,magnet=%u,segment=%u\n", seq,
                   s.last_cmd.mode_flags, s.last_cmd.trolley_cmd, s.last_cmd.hoist_cmd,
                   s.last_cmd.magnet_state, s.last_cmd.segment_idx);
        }
    }
    load_ack_payload();   // telemetry for the NEXT ACK carries seq_ack = this seq
}

static void poll_rx(void)
{
    for (int guard = 0; guard < 3; guard++) {
        uint8_t fifo = nrf24_read_reg(&s_radio, NRF_REG_FIFO_STATUS);
        if (fifo & NRF_FIFO_RX_EMPTY) {
            break;
        }
        uint8_t st = nrf24_status(&s_radio);
        int pipe = NRF_STATUS_RX_P_NO(st);
        int len = s.profile_icd ? nrf24_rx_payload_width(&s_radio) : RIG_PAYLOAD_BYTES;
        if (len < 1 || len > 32) {
            // Corrupt width (spec 7.3.4): the only safe action is to flush.
            nrf24_flush_rx(&s_radio);
            nrf24_write_reg(&s_radio, NRF_REG_STATUS, NRF_STATUS_RX_DR);
            s.bad++;
            printf("RIG,WARN,bad payload width %d - RX FIFO flushed\n", len);
            break;
        }
        uint8_t buf[32];
        nrf24_read_payload(&s_radio, buf, len);
        nrf24_write_reg(&s_radio, NRF_REG_STATUS, NRF_STATUS_RX_DR);
        handle_frame(buf, len, pipe);
    }
}

// -----------------------------------------------------------------------------
// Diagnostics
// -----------------------------------------------------------------------------
static void diag_scan(int passes)
{
    static uint16_t hits[126];
    memset(hits, 0, sizeof(hits));
    printf("RIG,SCAN_START,passes=%d\n", passes);
    for (int p = 0; p < passes; p++) {
        for (int ch = 0; ch <= 125; ch++) {
            nrf24_ce(&s_radio, 0);
            nrf24_write_reg(&s_radio, NRF_REG_RF_CH, (uint8_t)ch);
            nrf24_ce(&s_radio, 1);
            esp_rom_delay_us(200);
            if (nrf24_read_reg(&s_radio, NRF_REG_RPD) & 0x01) hits[ch]++;
        }
        vTaskDelay(1);
    }
    uint32_t total = 0;
    int busiest = 0;
    for (int ch = 0; ch <= 125; ch++) {
        printf("RIG,SCAN,%d,%u,%d\n", ch, hits[ch], passes);
        total += hits[ch];
        if (hits[ch] > hits[busiest]) busiest = ch;
    }
    printf("RIG,SCAN_END,%" PRIu32 ",%d\n", total, busiest);
    apply_config();
}

static void diag_rpd(int ms)
{
    uint32_t hits = 0, samples = 0;
    const int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    while (esp_timer_get_time() < end) {
        nrf24_ce(&s_radio, 0);
        nrf24_ce(&s_radio, 1);
        esp_rom_delay_us(200);
        samples++;
        if (nrf24_read_reg(&s_radio, NRF_REG_RPD) & 0x01) hits++;
        if ((samples & 0x3F) == 0) vTaskDelay(1);
    }
    printf("RIG,RPD,%" PRIu32 ",%" PRIu32 "\n", hits, samples);
    apply_config();
}

// Constant carrier (spec appendix C) so the ground station's RPD can hear us.
static void diag_carrier(int ms, int ch)
{
    nrf24_ce(&s_radio, 0);
    nrf24_write_reg(&s_radio, NRF_REG_CONFIG, NRF_CONFIG_PWR_UP);   // PTX, no CRC
    vTaskDelay(pdMS_TO_TICKS(3));
    nrf24_write_reg(&s_radio, NRF_REG_EN_AA, 0x00);
    nrf24_write_reg(&s_radio, NRF_REG_SETUP_RETR, 0x00);
    nrf24_write_reg(&s_radio, NRF_REG_RF_SETUP,
                    (uint8_t)(NRF_RF_CONT_WAVE | NRF_RF_PLL_LOCK | rf_setup_value(s.mode, 3)));
    nrf24_write_reg(&s_radio, NRF_REG_RF_CH, (uint8_t)ch);
    if (s_radio.is_plus) {
        // nRF24L01+: keep a payload re-transmitting under CONT_WAVE (same method as
        // RF24::startConstCarrier on the ground station) so the carrier is solid.
        uint8_t ff[32];
        memset(ff, 0xFF, sizeof(ff));
        nrf24_flush_tx(&s_radio);
        nrf24_write_tx_payload(&s_radio, ff, sizeof(ff));
    }
    nrf24_ce(&s_radio, 1);
    if (s_radio.is_plus) {
        vTaskDelay(pdMS_TO_TICKS(1));
        nrf24_command(&s_radio, 0xE3);   // REUSE_TX_PL
        nrf24_ce(&s_radio, 0);
        nrf24_ce(&s_radio, 1);
    }
    printf("RIG,CARRIER,ON,ch=%d,ms=%d,pa=MAX\n", ch, ms);
    vTaskDelay(pdMS_TO_TICKS(ms));
    // Spec: with REUSE_TX_PL + CONT_WAVE the chip ignores CE low; PWR_UP = 0 stops it.
    nrf24_write_reg(&s_radio, NRF_REG_CONFIG, 0x00);
    nrf24_ce(&s_radio, 0);
    nrf24_write_reg(&s_radio, NRF_REG_RF_SETUP, rf_setup_value(s.mode, s.pa));
    nrf24_flush_tx(&s_radio);
    printf("RIG,CARRIER,OFF\n");
    apply_config();
}

// -----------------------------------------------------------------------------
// Console commands (executed in the radio task, so they never race the RX path)
// -----------------------------------------------------------------------------
static int parse_pa(const char *t)
{
    if (!t) return -1;
    if (!strcmp(t, "MIN") || !strcmp(t, "0")) return 0;
    if (!strcmp(t, "LOW") || !strcmp(t, "1")) return 1;
    if (!strcmp(t, "HIGH") || !strcmp(t, "2")) return 2;
    if (!strcmp(t, "MAX") || !strcmp(t, "3")) return 3;
    return -1;
}

static void print_heartbeat(const char *tag)
{
    const uint32_t up = now_ms() / 1000;
    const uint32_t period_frames = s.frames - s.hb_frames_prev;
    s.hb_frames_prev = s.frames;
    int ce_gpio, ce_mcp;
    ce_readback(&ce_gpio, &ce_mcp);
    printf("RIG,%s,up=%" PRIu32 ",frames=%" PRIu32 ",pings=%" PRIu32 ",moves=%" PRIu32 ",bad=%" PRIu32
           ",dups=%" PRIu32 ",period_frames=%" PRIu32 ",link=%s,profile=%s,mode=%s,ch=%d,pa=%s,"
           "status=%02X,fifo=%02X,config=%02X,ce_j7_10=%d,ce_j3_3=%d,reinit=%" PRIu32 ",tlm=%s\n",
           tag, up, s.frames, s.pings, s.moves, s.bad, s.dups, period_frames, s.link_ok ? "OK" : "LOST",
           s.profile_icd ? "ICD" : "STATIC", mode_name(s.mode), s.channel, nrf24_pa_name(s.pa),
           nrf24_status(&s_radio), nrf24_read_reg(&s_radio, NRF_REG_FIFO_STATUS),
           nrf24_read_reg(&s_radio, NRF_REG_CONFIG), ce_gpio, ce_mcp, s.reinit,
           rig_tlm_is_model() ? "MODEL" : "ZERO");
    rig_tlm_print();
    loadcell_print("LOAD");
}

static void print_help(void)
{
    printf("RIG,HELP,HELP INFO STATS CLEAR PROFILE <ICD|STATIC> MODE <APP|CTRL> PA <MIN|LOW|HIGH|MAX> "
           "CH <0-125> LOG <0|1> TLM <MODEL|ZERO> LOAD [TARE|CAL <kg>|WATCH [s]|RESET] SCAN [passes] RPD [ms] CARRIER [ms] [ch] CE <0|1> REINIT SELFTEST\n");
    printf("OK,HELP\n");
}

static void print_info(void)
{
    printf("RIG,INFO,fw=%s,name=%s,variant=%s,probe=%s,setup_aw=%02X,spi_hz=%d,csn=GPIO%d(J1.1),"
           "sck=GPIO%d,mosi=GPIO%d,miso=GPIO%d,pwr=GPIO%d(+3.3VP J1.8)\n",
           RIG_FW_VERSION, RIG_FW_NAME, s_radio.is_plus ? "nRF24L01+" : "nRF24L01(non+)",
           s.probe_err == ESP_OK ? "OK" : esp_err_to_name(s.probe_err), s.probe_setup_aw, RIG_NRF_SPI_HZ,
           RIG_NRF_CSN_GPIO, RIG_SPI_SCK_GPIO, RIG_SPI_MOSI_GPIO, RIG_SPI_MISO_GPIO, RIG_PERIPH_PWR_EN_GPIO);
    print_link_line("LINK");
    print_regs();
    print_addr();
    print_heartbeat("STATS");
    printf("OK,INFO\n");
}

static void selftest(void)
{
    printf("RIG,SELFTEST,start: step 1/2 receiver scan (20 passes), step 2/2 carrier 5 s on ch%d\n", s.channel);
    diag_scan(20);
    diag_carrier(5000, s.channel);
    printf("RIG,SELFTEST,done\n");
}

static void handle_line(char *line)
{
    for (char *p = line; *p; p++) *p = (char)toupper((unsigned char)*p);
    char *save = NULL;
    char *cmd = strtok_r(line, " \t", &save);
    if (!cmd) {
        print_link_line("LINK");
        return;
    }
    char *a1 = strtok_r(NULL, " \t", &save);
    char *a2 = strtok_r(NULL, " \t", &save);

    if (!s.radio_ok && strcmp(cmd, "HELP") && strcmp(cmd, "INFO") && strcmp(cmd, "REINIT") && strcmp(cmd, "LOAD")) {
        printf("ERR,radio not responding on SPI (probe=%s) - check J1 wiring / 3.3VP, then REINIT\n",
               esp_err_to_name(s.probe_err));
        return;
    }

    if (!strcmp(cmd, "HELP")) {
        print_help();
    } else if (!strcmp(cmd, "INFO")) {
        print_info();
    } else if (!strcmp(cmd, "STATS")) {
        print_heartbeat("STATS");
        printf("OK,STATS\n");
    } else if (!strcmp(cmd, "CLEAR")) {
        s.frames = s.pings = s.moves = s.bad = s.dups = s.hb_frames_prev = 0;
        s.last_seq = -1;
        printf("OK,CLEAR\n");
    } else if (!strcmp(cmd, "PROFILE")) {
        if (a1 && (!strcmp(a1, "ICD") || !strcmp(a1, "STATIC"))) {
            s.profile_icd = !strcmp(a1, "ICD");
            int e = apply_config();
            print_link_line("LINK");
            printf("OK,PROFILE,%s,cfg_errors=%d\n", s.profile_icd ? "ICD" : "STATIC", e);
        } else {
            printf("ERR,usage: PROFILE <ICD|STATIC>\n");
        }
    } else if (!strcmp(cmd, "MODE")) {
        if (a1 && (!strcmp(a1, "APP") || !strcmp(a1, "CTRL"))) {
            link_mode_t m = !strcmp(a1, "APP") ? MODE_APP : MODE_CTRL;
            if (m == MODE_CTRL && !s_radio.is_plus) {
                printf("ERR,250 kbps needs an nRF24L01+\n");
                return;
            }
            s.mode = m;
            s.channel = (m == MODE_APP) ? RIG_APP_CHANNEL : RIG_CTRL_CHANNEL;
            int e = apply_config();
            print_link_line("LINK");
            printf("OK,MODE,%s,cfg_errors=%d\n", mode_name(s.mode), e);
        } else {
            printf("ERR,usage: MODE <APP|CTRL>\n");
        }
    } else if (!strcmp(cmd, "PA")) {
        int pa = parse_pa(a1);
        if (pa < 0) {
            printf("ERR,usage: PA <MIN|LOW|HIGH|MAX>\n");
            return;
        }
        s.pa = pa;
        int e = apply_config();
        printf("OK,PA,%s,cfg_errors=%d\n", nrf24_pa_name(s.pa), e);
    } else if (!strcmp(cmd, "CH")) {
        int ch = a1 ? atoi(a1) : -1;
        if (ch < 0 || ch > 125) {
            printf("ERR,usage: CH <0-125>\n");
            return;
        }
        s.channel = ch;
        int e = apply_config();
        printf("OK,CH,%d,cfg_errors=%d\n", s.channel, e);
    } else if (!strcmp(cmd, "LOG")) {
        s.log_frames = a1 ? atoi(a1) != 0 : !s.log_frames;
        printf("OK,LOG,%d\n", s.log_frames ? 1 : 0);
    } else if (!strcmp(cmd, "LOAD")) {
        loadcell_command(a1, a2);
    } else if (!strcmp(cmd, "TLM")) {
        if (a1 && (!strcmp(a1, "MODEL") || !strcmp(a1, "ZERO"))) {
            rig_tlm_set_model(!strcmp(a1, "MODEL"));
            load_ack_payload();
        } else if (a1) {
            printf("ERR,usage: TLM <MODEL|ZERO>\n");
            return;
        }
        rig_tlm_print();
        printf("OK,TLM,%s\n", rig_tlm_is_model() ? "MODEL" : "ZERO");
    } else if (!strcmp(cmd, "SCAN")) {
        int passes = a1 ? atoi(a1) : 20;
        if (passes < 1 || passes > 1000) passes = 20;
        diag_scan(passes);
        printf("OK,SCAN\n");
    } else if (!strcmp(cmd, "RPD")) {
        int ms = a1 ? atoi(a1) : 2000;
        if (ms < 10 || ms > 60000) ms = 2000;
        diag_rpd(ms);
        printf("OK,RPD\n");
    } else if (!strcmp(cmd, "CARRIER")) {
        int ms = a1 ? atoi(a1) : 5000;
        int ch = a2 ? atoi(a2) : s.channel;
        if (ms < 10 || ms > 60000) ms = 5000;
        if (ch < 0 || ch > 125) ch = s.channel;
        diag_carrier(ms, ch);
        printf("OK,CARRIER\n");
    } else if (!strcmp(cmd, "CE")) {
        int lvl = a1 ? atoi(a1) : 1;
        nrf24_ce(&s_radio, lvl);
        int g, m;
        ce_readback(&g, &m);
        printf("OK,CE,%d,ce_j7_10=%d,ce_j3_3=%d\n", lvl ? 1 : 0, g, m);
    } else if (!strcmp(cmd, "REINIT")) {
        s.probe_err = nrf24_probe(&s_radio, &s.probe_setup_aw);
        s.radio_ok = (s.probe_err == ESP_OK);
        if (s.radio_ok) {
            nrf24_detect_variant(&s_radio);
            int e = apply_config();
            s.reinit++;
            print_link_line("LINK");
            printf("OK,REINIT,cfg_errors=%d\n", e);
        } else {
            printf("ERR,probe failed (%s, SETUP_AW=%02X)\n", esp_err_to_name(s.probe_err), s.probe_setup_aw);
        }
    } else if (!strcmp(cmd, "SELFTEST")) {
        selftest();
        printf("OK,SELFTEST\n");
    } else {
        printf("ERR,unknown command '%s' (try HELP)\n", cmd);
    }
}

// -----------------------------------------------------------------------------
// Radio task
// -----------------------------------------------------------------------------
static void radio_task(void *arg)
{
    (void)arg;
    uint32_t next_hb = now_ms() + RIG_HEARTBEAT_MS;
    uint32_t next_tlm = now_ms() + RIG_TLM_TICK_MS;
    int boot_btn_prev = 1;
    uint32_t boot_btn_since = 0;
    char line[LINE_MAX_LEN];

    for (;;) {
        while (xQueueReceive(s_lines, line, 0) == pdTRUE) {
            handle_line(line);
        }

        if (s.radio_ok) {
            poll_rx();

            const bool fresh = (esp_timer_get_time() - s.last_rx_us) < (int64_t)RIG_LINK_TIMEOUT_MS * 1000;
            if (s.link_ok && !fresh) {
                s.link_ok = false;
                status_led_set_rx_mode(false);
                printf("RIG,WARN,link lost (no frame for %d ms) - safe hold\n", RIG_LINK_TIMEOUT_MS);
            }
            if ((int32_t)(now_ms() - next_tlm) >= 0) {
                next_tlm = now_ms() + RIG_TLM_TICK_MS;
                rig_tlm_tick(esp_timer_get_time(), s.link_ok);
                if (!s.link_ok) load_ack_payload();   // keep the waiting reply current while idle
            }
        }

        // BOOT button: run the self-test without needing to type anything.
        int btn = gpio_get_level(RIG_BOOT_BUTTON_GPIO);
        if (btn == 0 && boot_btn_prev == 1) boot_btn_since = now_ms();
        if (btn == 0 && boot_btn_since && now_ms() - boot_btn_since > 50) {
            boot_btn_since = 0;
            if (s.radio_ok) selftest();
        }
        boot_btn_prev = btn;

        if ((int32_t)(now_ms() - next_hb) >= 0) {
            next_hb += RIG_HEARTBEAT_MS;
#if RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3
            // CE lives on the I2C expander: make sure J3.3 still carries what we last wrote.
            if (s_mcp && s_radio.ce_level >= 0) {
                int g, m;
                ce_readback(&g, &m);
                if (m != s_radio.ce_level) {
                    printf("RIG,WARN,CE on J3.3 reads %d, expected %d - re-driving the MCP23017\n", m, s_radio.ce_level);
                    mcp23017_set_direction(s_mcp, MCP23017_PORT_A, (uint8_t)~(1u << RIG_NRF_CE_MCP_BIT));
                    nrf24_ce(&s_radio, s_radio.ce_level);
                }
            }
#endif
            if (s.radio_ok) {
                const uint8_t cfg = nrf24_read_reg(&s_radio, NRF_REG_CONFIG);
                if ((cfg & 0x7F) != (s.expect_config & 0x7F)) {
                    // Brown-out / module reset: CONFIG returns to 0x08 after power-on.
                    printf("RIG,WARN,radio CONFIG=%02X (expected %02X) - module reset or brown-out, reconfiguring\n",
                           cfg, s.expect_config);
                    s.reinit++;
                    apply_config();
                }
                print_heartbeat("HB");
            } else {
                printf("RIG,HB,up=%" PRIu32 ",radio=NOT_RESPONDING,probe=%s,setup_aw=%02X\n", now_ms() / 1000,
                       esp_err_to_name(s.probe_err), s.probe_setup_aw);
            }
        }
        vTaskDelay(1);   // CONFIG_FREERTOS_HZ=1000 -> 1 ms poll period
    }
}

void rf_link_submit_line(const char *line)
{
    if (!s_lines) return;
    char buf[LINE_MAX_LEN];
    strncpy(buf, line, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    if (xQueueSend(s_lines, buf, 0) != pdTRUE) {
        printf("ERR,busy - command dropped\n");
    }
}

esp_err_t rf_link_start(mcp23017_handle_t *mcp)
{
    memset(&s, 0, sizeof(s));
    s.profile_icd = RIG_DEFAULT_PROFILE_ICD;
    s.mode = MODE_APP;
    s.channel = RIG_APP_CHANNEL;
    s.pa = RIG_DEFAULT_PA;
    s.log_frames = true;
    s.last_seq = -1;
    s.last_rx_us = -(int64_t)RIG_LINK_TIMEOUT_MS * 1000 * 10;
    s_mcp = mcp;
    rig_tlm_init(RIG_TLM_DEFAULT_MODEL);

    // CE outputs, LOW until the radio is configured.
#if RIG_CE_PATHS & RIG_CE_PATH_GPIO_J7_10
    gpio_config_t ce_cfg = {
        .pin_bit_mask = 1ULL << RIG_NRF_CE_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT,   // so the level can be read back
    };
    gpio_config(&ce_cfg);
    gpio_set_level(RIG_NRF_CE_GPIO, 0);
#endif
#if RIG_CE_PATHS & RIG_CE_PATH_MCP_J3_3
    if (s_mcp) {
        s_mcp_olat_a = 0x00;
        mcp23017_write_port(s_mcp, MCP23017_PORT_A, s_mcp_olat_a);                      // latch LOW first
        mcp23017_set_direction(s_mcp, MCP23017_PORT_A, (uint8_t)~(1u << RIG_NRF_CE_MCP_BIT)); // GPA0 output
    } else {
#if RIG_CE_PATHS == RIG_CE_PATH_MCP_J3_3
        ESP_LOGE(TAG, "MCP23017 (I2C 0x20) not available - CE on J3.3 CANNOT be driven, so the radio "
                      "cannot receive. Check the I2C bus / PX_RESET (GPIO5).");
        printf("RIG,ERR,CE path J3.3 unavailable: MCP23017 did not probe\n");
#else
        ESP_LOGW(TAG, "MCP23017 not available - CE cannot be driven on J3.3");
#endif
    }
#endif
    gpio_config_t btn_cfg = {
        .pin_bit_mask = 1ULL << RIG_BOOT_BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&btn_cfg);

    esp_err_t err = nrf24_attach(&s_radio, RIG_SPI_HOST, RIG_NRF_CSN_GPIO, RIG_NRF_SPI_HZ, ce_write, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device for the nRF24 failed (%s)", esp_err_to_name(err));
        return err;
    }

    vTaskDelay(pdMS_TO_TICKS(110));   // nRF power-on reset (100 ms) after +3.3VP came up
    s.probe_err = nrf24_probe(&s_radio, &s.probe_setup_aw);
    s.radio_ok = (s.probe_err == ESP_OK);
    if (s.radio_ok) {
        nrf24_detect_variant(&s_radio);
        ESP_LOGI(TAG, "probe OK: SPI read-back good, SETUP_AW=0x%02X, %s", s.probe_setup_aw,
                 s_radio.is_plus ? "nRF24L01+" : "nRF24L01 (non +)");
        int e = apply_config();
        print_link_line("BOOT");
        print_regs();
        print_addr();
        if (e == 0) {
            ESP_LOGI(TAG, "link up: PRX on 'REII2' ch%d/%s %s, listening", s.channel, rate_name(s.mode),
                     s.profile_icd ? "dyn32+ACK-payload telemetry (ICD)" : "static32");
        } else {
            ESP_LOGE(TAG, "%d register(s) did not read back - see RIG,ERR lines", e);
        }
    } else {
        ESP_LOGE(TAG, "nRF24 NOT RESPONDING (%s, SETUP_AW=0x%02X). Check: nRF VCC on J1.8 (+3.3VP), "
                      "GND J1.5/6, CSN J1.1, SCK J1.3, MOSI J1.2, MISO J1.4, 10uF+100nF at the module.",
                 esp_err_to_name(s.probe_err), s.probe_setup_aw);
        printf("RIG,BOOT,radio=NOT_RESPONDING,probe=%s,setup_aw=%02X\n", esp_err_to_name(s.probe_err),
               s.probe_setup_aw);
    }

    s_lines = xQueueCreate(8, LINE_MAX_LEN);
    // Priority above the sensor poll tasks so a burst of frames is never left in the FIFO.
    xTaskCreate(radio_task, "rf_link", 6144, NULL, tskIDLE_PRIORITY + 5, NULL);
    return s.radio_ok ? ESP_OK : s.probe_err;
}
