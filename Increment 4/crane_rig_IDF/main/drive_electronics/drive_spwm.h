// =============================================================================
// drive_spwm.h - 3-phase SPWM for the EEII 1 VFD (ICD IF6.0), 10 kHz carrier
// =============================================================================
// Added with the drive electronics firmware (Increment 4, 17 Nov 2026).
// Replaces the starter spwm.c for this job (that driver stays in the build,
// switched off by RIG_ENABLE_SPWM 0; it was fixed at 20 kHz, started at boot
// and could not be stopped, re-scaled or reversed).
//
// Signal chain per phase (EEII 1 board, ICD 3.3.2 / 3.3.4):
//
//   ESP32-S3 GPIO --R--> 4N25 LED   4N25 transistor + pull-up --> IR2104 IN --> HO/LO --> IGBT leg
//   (J7.8/7.7/7.6)                  (INVERTS: LED on -> IN low)   (520 ns built-in dead time)
//
// Everything in this module is worked out in the "IN domain": duty = the
// fraction of each 100 us carrier period that the IR2104 IN pin is HIGH
// (= high-side IGBT on). Because the opto inverts, the MCPWM generator output
// is inverted in the GPIO matrix (flag invert_pwm), so the pin is HIGH while
// IN is LOW. Set opto_inverts = false if a non-inverting stage is ever fitted.
//
// How the sine is made (same idea as the starter, now with amplitude control):
//   - One MCPWM timer, up-counting at 40 MHz, period 4000 ticks = 10 kHz.
//   - At every timer-empty event (every 100 us) an ISR advances one 32-bit
//     phase accumulator by step = f_sine / f_carrier * 2^32 and looks up a
//     256-entry sine table: compare = half + m * half * sin(phase).
//   - Phase B lags A by 120 deg, C lags by 240 deg (sequence A-B-C). Reverse
//     swaps B and C (A-C-B). Swapping is only allowed from IDLE (EEII 1: stop first).
//   - compare is clamped to [duty_min, duty_max] so no pulse is shorter than
//     the opto can pass (see main.c for the assumed values).
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int gpio_a, gpio_b, gpio_c;   // J7.8 / J7.7 / J7.6 = GPIO38 / 39 / 40
    uint32_t carrier_hz;          // 10 000 (EEII 1, 17 Nov)
    bool opto_inverts;            // 4N25 stage inverts (EEII 1, 17 Nov)
    int idle_in_level;            // IN level held while stopped: 0 = IR2104 IN LOW (low-side IGBTs on)
    float duty_min, duty_max;     // IN-domain duty clamps for SINE mode (0..1)
    bool max_drive_strength;      // GPIO drive capability 3 (~40 mA) for the opto LEDs
} drive_spwm_cfg_t;

typedef enum {
    DRIVE_SPWM_IDLE = 0,          // all three pins forced to the idle level, no switching
    DRIVE_SPWM_SINE = 1,          // 3-phase SPWM
    DRIVE_SPWM_FIXED = 2,         // test: fixed duty square wave on selected phases
} drive_spwm_mode_t;

#define DRIVE_PHASE_A 0x01
#define DRIVE_PHASE_B 0x02
#define DRIVE_PHASE_C 0x04
#define DRIVE_PHASE_ALL 0x07

esp_err_t drive_spwm_init(const drive_spwm_cfg_t *cfg);

/** Stop switching: all phases held at idle_in_level. */
void drive_spwm_idle(void);

/** Start SPWM from IDLE (or FIXED). reverse = phase order A-C-B. Returns
 *  ESP_ERR_INVALID_STATE if already running SINE in the other direction. */
esp_err_t drive_spwm_start_sine(float freq_hz, float m, bool reverse);

/** Change frequency / modulation index while running SINE (glitch-free, the
 *  ISR picks the new values up at the next carrier period). */
void drive_spwm_set_sine(float freq_hz, float m);

/** Test: fixed-duty square wave at the carrier frequency on the phases in
 *  phase_mask, measured AT THE MCU PIN (e.g. 0.5 = the 10 kHz 50 % "yellow"
 *  signal-generator trace). Other phases idle. No duty clamps. */
esp_err_t drive_spwm_fixed(uint8_t phase_mask, float pin_duty);

drive_spwm_mode_t drive_spwm_mode(void);
float drive_spwm_freq(void);          // sine frequency (SINE mode)
float drive_spwm_m(void);             // modulation index (SINE mode)
bool drive_spwm_reversed(void);
uint8_t drive_spwm_phase_mask(void);  // FIXED mode
float drive_spwm_fixed_duty(void);    // FIXED mode, at the pin
uint32_t drive_spwm_period_ticks(void);
uint32_t drive_spwm_isr_count(void);  // carrier periods seen (proves the ISR runs)

#ifdef __cplusplus
}
#endif
