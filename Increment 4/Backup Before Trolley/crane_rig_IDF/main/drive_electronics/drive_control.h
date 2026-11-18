// =============================================================================
// drive_control.h - hoist drive (EEII 1 VFD, ICD IF6.0 + brake IF15.0) on the crane MCU
// =============================================================================
// Added with the drive electronics firmware (Increment 4, 17 Nov 2026).
//
// What it does
//   * Takes hoist_cmd (Table 33 byte 5, -100..+100 %, + = up) from every MOVE
//     frame the dashboard sends and turns it into the VFD signals:
//       J7.8 / J7.7 / J7.6  VFD_SPWM_A/B/C   3-phase SPWM, 10 kHz carrier (drive_spwm.c)
//       J3.4 (MCP GPA1)     DRV_EN_LOGIC     HIGH = DC-bus IGBT on (EEII 1)
//       J4.8 (MCP GPB5)     HOIST_BRAKE      HIGH = release (ICD Table 38)
//   * Reads the drive's inputs:
//       J3.5 (MCP GPA2)     DRV_FAULT_IN     -> Table 34 fault b6 (VFD fault)
//       J3.8 (MCP GPA5)     VFD_OC_IN        -> Table 34 fault b0 (VFD overcurrent, 10 A)
//   * Runs the start / stop / reverse sequence in its own 100 Hz task:
//
//       IDLE --cmd!=0--> STARTING: DRV_EN on, SPWM at f_start, wait brake delay
//            --> brake released --> RUN: ramp f towards |cmd| * f_max, V/f amplitude
//       RUN --cmd 0 / direction change / lost link / soft stop--> STOPPING:
//            ramp down to f_start, clamp the brake, wait brake delay,
//            SPWM idle, DRV_EN off --> (direction change) REVERSE_DWELL --> IDLE
//       any state --fault input--> FAULT: everything off at once, latched
//            until RESET (dashboard Reset button = Table 33 b1, or DRIVE RESET)
//
//   * Console test modes (typed on COM12, see drive_command()), for measuring
//     one phase at a time with external instruments. The dashboard is ignored
//     while a test mode is active; DRIVE STOP hands control back.
//
// All assumed values (frequencies, V/f, ramps, input polarities, pins) are set
// in main.c, start_drive_electronics(), each marked ASSUMED / ADJUST HERE.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "icd_frames.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // --- SPWM outputs (drive_spwm.c) ---
    int spwm_gpio_a, spwm_gpio_b, spwm_gpio_c;
    uint32_t carrier_hz;
    bool opto_inverts;
    int idle_in_level;
    float duty_min, duty_max;

    // --- DRV_EN output (MCP23017 port A) ---
    int drv_en_bit;                 // GPA1 = J3.4
    bool drv_en_active_high;

    // --- Hoist brake output (MCP23017 port B) ---
    bool brake_enabled;
    int brake_bit;                  // GPB5 = J4.8
    bool brake_release_high;
    uint32_t brake_delay_ms;        // motor energised this long before the brake opens / after it closes

    // --- Inputs (MCP23017 port A) ---
    bool fault_enabled;
    int fault_bit;                  // GPA2 = J3.5
    int fault_active_level;
    bool fault_pullup;
    bool oc_enabled;
    int oc_bit;                     // GPA5 = J3.8
    int oc_active_level;
    bool oc_pullup;

    // --- Motion profile ---
    float f_max_hz;                 // frequency at |hoist_cmd| = 100 %
    float f_start_hz;               // lowest running frequency
    float f_base_hz;                // V/f base: m reaches m_max here
    float m_max;                    // modulation index at f_base
    float m_boost;                  // minimum modulation index (low-speed boost)
    float ramp_hz_s;                // normal acceleration / deceleration
    float stop_ramp_hz_s;           // lost link / soft stop deceleration
    uint32_t reverse_dwell_ms;      // standstill time before the phases are swapped
    int cmd_deadband_pct;           // |hoist_cmd| below this = 0
    bool up_is_abc;                 // true: UP = A-B-C, DOWN = A-C-B
    uint32_t link_timeout_ms;       // no MOVE frame for this long = stop (ICD 500 ms)
    float hoist_mm_s_at_fmax;       // telemetry estimate: hoist speed at f_max
} drive_cfg_t;

typedef struct {
    bool present;                   // drive_start() succeeded
    bool test_mode;                 // console test mode active
    const char *state;              // IDLE / STARTING / RUN / STOPPING / REVERSE_DWELL / FAULT / TEST
    bool drv_en;
    bool brake_released;
    float freq_hz;                  // applied sine frequency, 0 when not switching
    float m;                        // applied modulation index
    int dir;                        // +1 up, -1 down, 0 stopped
    float speed_pct;                // signed, freq / f_max * 100
    float hoist_vel_mm_s;           // estimate for Table 34 bytes 8-9
    uint8_t fault_flags;            // ICD_FAULT_VFD_OVERCURRENT / ICD_FAULT_VFD (latched)
} drive_status_t;

/** Configure the pins (all outputs safe: SPWM idle, DRV_EN off, brake clamped)
 *  and start the drive task. Needs mcp_shared_init() first for DRV_EN / brake /
 *  inputs; without the MCP23017 only the SPWM pins are driven. */
esp_err_t drive_start(const drive_cfg_t *cfg);

/** A Table 33 MOVE frame arrived (called from the radio task). */
void drive_on_move(const icd_downlink_t *cmd, int64_t now_us);

/** Console: DRIVE [sub] [arg1] [arg2] (already upper-case). */
void drive_command(const char *sub, const char *a1, const char *a2);

/** One machine-readable status line: RIG,<tag>,... */
void drive_print(const char *tag);

void drive_get_status(drive_status_t *out);

#ifdef __cplusplus
}
#endif
