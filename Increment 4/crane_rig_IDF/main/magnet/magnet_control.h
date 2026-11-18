// =============================================================================
// magnet_control.h - electromagnet (EEII 2 load attachment, ICD IF12.0) on the crane MCU
// =============================================================================
// Added with the magnet firmware (Increment 4, v4.7.0, 18 Nov 2026).
//
// Pins: ONLY "MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx" (final
// pinout), EEII 2:
//   J7.5 (silkscreen IO4)  MAG_PWM      -> GPIO41, LEDC PWM to the MOSFET module
//                                          (board map: J7.3-J7.8 = GPIO 1, 42, 41, 40, 39, 38)
//   J3.6 (MCP GPA3)        MAG_LOGIC    -> enable, HIGH while the magnet is engaged (EEII 2)
//   J6.8 (AN3) / J6.9      MAG_CURR_IN  <- current sensor, ADS1220 AIN3 (1 k + 2.2 nF on board)
//
// What it does
//   * magnet_state (Table 33 byte 6, 0 off / 1 on) from every MOVE frame:
//       ON : MAG_LOGIC HIGH, then PWM at on_duty (EEII 2: 15 kHz, 99 %)
//       OFF: PWM at release_duty (EEII 2: 10 % disengages), then MAG_LOGIC LOW
//     Boot: PWM 0 %, MAG_LOGIC LOW (nothing commanded yet).
//   * Lost link / soft stop: the magnet KEEPS its state (ICD safe hold: never
//     drop the load). Only an explicit OFF releases it.
//   * Current: the ADS1220 is shared with the load cell (loadcell_set_aux). Every
//     aux_period_ms the sensor voltage on AIN3 is read:
//       V = code / 2^23 * AVDD          (single-ended, VREF = AVDD)
//       I = (V - V_zero) / sensitivity  (sensitivity in V/A, negative: the sensor
//                                        output FALLS as magnet current rises)
//     V_zero is re-measured automatically while the magnet is off.
//   * Table 34: magnet_current = |I| x 10 (byte 14), magnet_status (byte 15):
//       0 off, 1 holding (on and |I| >= hold_threshold), 2 fault (on but |I| below
//       the threshold hold_timeout_ms after engaging) + fault b4. The fault is an
//       indication only: the magnet is never switched off by the firmware.
//   * Console test modes (COM12), see magnet_command(). Dashboard magnet commands
//     are ignored while a test mode is active; MAGNET STOP hands control back.
//
// All assumed values are set in main.c, start_magnet(), marked ASSUMED.
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
    // --- Outputs ---
    int pwm_gpio;                 // J7.5 = GPIO41
    uint32_t pwm_hz;              // 15 kHz (EEII 2)
    float on_duty_pct;            // 99 % engages (EEII 2)
    float release_duty_pct;       // 10 % disengages (EEII 2)
    float boot_duty_pct;          // before the first command
    int logic_bit;                // J3.6 = MCP23017 GPA3
    bool logic_on_high;           // HIGH while engaged (EEII 2)
    uint32_t logic_lead_ms;       // MAG_LOGIC set this long before the PWM goes to on_duty

    // --- Current sensor (ADS1220 AIN3 via the load-cell task) ---
    bool sense_enabled;
    uint8_t adc_channel;          // 3 = AIN3 = J6.8
    uint32_t aux_period_ms;       // how often the current is read
    float avdd_v;                 // ADS1220 reference (AVDD = +3.3VP)
    float zero_v;                 // sensor output at 0 A (start value)
    bool auto_zero;               // re-measure zero_v while the magnet has been off auto_zero_after_ms
    uint32_t auto_zero_after_ms;
    float auto_zero_window_v;     // auto-zero only follows readings within this of the configured zero_v
                                  //   (so a real current at the release duty is NOT zeroed away)
    float sens_v_per_a;           // sensor slope, V per A (sign included)
    float sensor_min_v;           // readings outside [min, max] = sensor fault (unplugged / shorted)
    float sensor_max_v;

    // --- Holding check ---
    bool hold_check;
    float hold_threshold_a;       // |I| at or above this = holding
    uint32_t hold_timeout_ms;     // still below it this long after engaging = fault b4

    // --- Warnings (console + status flags only; never switch anything) ---
    float release_warn_a;         // |I| above this while released = load may not let go
    float over_current_warn_a;    // |I| above this while engaged = more than the coils / sensor are rated for
} magnet_cfg_t;

typedef struct {
    bool present;
    bool test_mode;
    const char *state;            // OFF / ON / TEST
    bool engaged;                 // commanded on
    float duty_pct;
    bool logic;                   // MAG_LOGIC level (true = HIGH)
    bool current_valid;
    bool sensor_fault;            // reading outside the plausible window
    bool release_current_high;    // released, but |I| > release_warn_a
    bool over_current;            // engaged and |I| > over_current_warn_a
    float sensor_v;
    float zero_v;
    float current_a;              // signed, from the sensor
    uint8_t status;               // Table 34 byte 15: 0 off, 1 holding, 2 fault
    bool fault;
} magnet_status_t;

esp_err_t magnet_start(const magnet_cfg_t *cfg);
void magnet_on_move(const icd_downlink_t *cmd, int64_t now_us);
void magnet_command(const char *sub, const char *a1, const char *a2);
void magnet_print(const char *tag);
void magnet_get_status(magnet_status_t *out);

#ifdef __cplusplus
}
#endif
