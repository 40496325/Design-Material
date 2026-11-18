// =============================================================================
// loadcell.h - S-type load cell, ICD IF13.0 (rig_config.h RIG_LOADCELL_SOURCE)
// =============================================================================
// HX711 amplifier board (GND / CLK / DAT / VCC) - default:
//   VCC -> J3.11 (+3.3VP, NOT J3.2 = +5 V)   GND -> J3.1
//   DAT -> J7.11 (GPIO4)                     CLK -> J7.4 (GPIO42)
//   Channel A, gain 128, 10 or 80 samples/s (the board's RATE pin decides).
//
// Bare bridge on J6 through the on-board ADS1220 (ICD Table 38):
//   J6.1 EXC+   J6.2 EXC-   J6.5 SIG+ (AIN0)   J6.6 SIG- (AIN1)
//   20 samples/s, PGA gain 128, ratiometric to +3.3VP, 50 Hz rejection.
//
// Both: kg = (filtered_code - tare_code) * kg_per_code
//   tare        : taken at boot (ICD start-up sequence) and with LOAD TARE
//   kg_per_code : from rig_config.h (rated capacity + sensitivity) until
//                 LOAD CAL <kg> is run with a known mass on the hook
// Tare and calibration are saved in NVS and survive a reboot (tare is still
// re-taken at boot when RIG_LOADCELL_TARE_AT_BOOT = 1).
//
// Console (COM12): LOAD | LOAD TARE | LOAD CAL <kg> | LOAD WATCH [s] | LOAD RESET
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ads1220.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool running;        // ADS1220 configured and converting
    bool valid;          // at least one full filter window since start / tare
    bool saturated;      // input at the ADC limit - wiring fault or massive overload
    int32_t raw;         // latest 24-bit code
    float filtered;      // moving average of the codes
    float mv_per_v;      // bridge output, mV per V of excitation
    float kg;            // calibrated weight (can be negative)
    int32_t tare;
    float kg_per_code;
    bool calibrated;     // kg_per_code came from LOAD CAL (not the datasheet default)
    uint32_t samples;
    uint32_t errors;
} loadcell_status_t;

/** Start the load cell. adc = ADS1220 handle (ADS1220 source) or NULL (HX711 source). */
esp_err_t loadcell_start(ads1220_handle_t *adc);

/** Thread-safe copy of the latest state. */
void loadcell_get(loadcell_status_t *out);

/** Console sub-command after "LOAD" (args already upper-case). Never blocks. */
void loadcell_command(const char *a1, const char *a2);

/** One machine-readable line: RIG,LOAD,... */
void loadcell_print(const char *tag);

/**
 * MAGNET (v4.7.0) - shared ADS1220 side channel (README C4, decision D10).
 * The load cell owns the ADS1220 (AIN0-AIN1 bridge, continuous). With a side
 * channel set, every period_ms the load-cell task takes TWO single-ended
 * conversions of AINx against AVSS (gain 1, PGA bypassed, VREF = AVDD; the
 * first is discarded in case it holds a stale bridge result), restores the
 * bridge configuration and discards the next bridge sample. Cost: ~3 of the
 * 20 load-cell samples per side reading. channel 0xFF = off.
 * ADS1220 load-cell source only (no-op with the HX711).
 */
void loadcell_set_aux(uint8_t channel, uint32_t period_ms);

/** Latest side-channel result: raw 24-bit code (full scale = VREF = AVDD)
 *  and when it was taken. Returns false if no valid reading yet. */
bool loadcell_get_aux(int32_t *code, int64_t *when_us, uint32_t *count);

#ifdef __cplusplus
}
#endif
