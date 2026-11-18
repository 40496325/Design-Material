// =============================================================================
// trolley_control.h - trolley (MCTR 1 lateral movement, ICD IF1.0) on the crane MCU
// =============================================================================
// Added with the trolley firmware (Increment 4, 17 Nov 2026).
//
// What it does
//   * Takes trolley_cmd (Table 33 byte 4, -100..+100 %) from every MOVE frame:
//       sign      -> direction: + = IN1 HIGH / IN2 LOW (FWD), - = IN1 LOW / IN2 HIGH (REV)
//       |cmd| %   -> ENA PWM duty on J7.9 (ICD: duty = |trolley_cmd| %)
//     Which end of the rail "+" is does not matter (MCTR 1 / REII, 17 Nov).
//   * Counts the wheel encoder (J7.12, single channel, rising edges). The sign
//     of each count comes from the commanded direction (ICD Table 15).
//     Position (mm) = counts x pi x wheel diameter / counts per wheel turn.
//   * Sends real trolley_pos / trolley_vel in Table 34 (bytes 2-3, 6-7).
//   * Runs in its own 100 Hz task:
//       IDLE --cmd!=0--> set IN1/IN2 for the direction, ramp ENA duty up
//       RUN  --cmd 0 / lost link / soft stop / rail limit--> ramp duty to 0, IN1=IN2=LOW (coast)
//       direction change: ramp to 0, coast until the encoder shows standstill (and at
//       least reverse_dwell_ms), then the other way
//   * Slow-down zone: within slow_zone_mm of an armed limit, moving towards it, the
//     duty is capped so the trolley can stop inside the margin.
//   * Rail limits (20 m rail): armed after the position is zeroed with the
//     trolley at the end it reaches on a "-" command (dashboard Zero Position =
//     Table 33 b3, or TROLLEY ZERO). Armed: no "+" past rail - margin, no "-"
//     below margin; Table 34 fault b3 (rail limit) while it is stopping it.
//   * Encoder check: driving >= enc_check_min_duty for enc_check_ms with no
//     counts -> Table 34 fault b5 (encoder). Indication only, does not stop.
//   * Console test modes (COM12), see trolley_command(). The dashboard is
//     ignored while a test mode is active; TROLLEY STOP hands control back.
//
// All assumed values are set in main.c, start_trolley(), marked ASSUMED.
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
    // --- Pins (wiring document) ---
    int pwm_gpio;                 // J7.9 = GPIO18
    uint32_t pwm_hz;
    int in1_bit, in2_bit;         // J4.4 = GPB1, J4.5 = GPB2
    bool mirror_in34;
    int in3_bit, in4_bit;         // J4.6 = GPB3, J4.7 = GPB4
    int enc_gpio;                 // J7.12 = GPIO21
    bool enc_pullup;
    uint32_t enc_glitch_ns;

    // --- Mechanics ---
    float wheel_diameter_mm;
    float counts_per_wheel_rev;
    float rail_length_mm;
    float limit_margin_mm;        // stop this far before each end
    bool soft_limits;             // enforce the rail limits once the position has been zeroed
    float slow_zone_mm;           // within this distance of a limit (moving towards it) ...
    int slow_zone_duty_pct;       // ... the ENA duty is capped at this

    // --- Motion profile ---
    float slew_pct_s;             // ENA duty change rate, normal
    float stop_slew_pct_s;        // ENA duty change rate, lost link / soft stop / rail limit
    int min_run_duty_pct;         // smallest duty applied while moving (0 = duty follows the command)
    int max_duty_pct;             // cap on ENA duty
    int cmd_deadband_pct;         // |trolley_cmd| below this = stop
    uint32_t reverse_dwell_ms;    // coast at least this long before running the other way ...
    uint32_t standstill_ms;       // ... and until the encoder has seen no counts for this long
    uint32_t reverse_max_wait_ms; // give up waiting for standstill after this (encoder missing / noisy)
    uint32_t link_timeout_ms;     // no MOVE frame for this long = stop (ICD 500 ms)

    // --- Encoder check ---
    bool enc_check;
    int enc_check_min_duty_pct;
    uint32_t enc_check_ms;
} trolley_cfg_t;

typedef struct {
    bool present;
    bool test_mode;
    const char *state;            // IDLE / RUN / STOPPING / REVERSE_DWELL / TEST
    int dir;                      // +1 FWD, -1 REV, 0 stopped
    float duty_pct;               // applied ENA duty
    float speed_pct;              // signed duty (Table 34 / model)
    float pos_mm;                 // encoder position since zero
    float vel_mm_s;               // encoder speed, signed by direction
    bool zeroed;                  // position zeroed -> limits armed
    bool rail_limit;              // a rail limit is stopping the trolley
    bool encoder_fault;
    uint32_t enc_total;
} trolley_status_t;

esp_err_t trolley_start(const trolley_cfg_t *cfg);
void trolley_on_move(const icd_downlink_t *cmd, int64_t now_us);
void trolley_command(const char *sub, const char *a1, const char *a2);
void trolley_print(const char *tag);
void trolley_get_status(trolley_status_t *out);

#ifdef __cplusplus
}
#endif
