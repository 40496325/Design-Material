// Sinusoidal PWM (SPWM) generator using the ESP32-S3's MCPWM (Motor Control
// PWM) peripheral. This is the building block behind driving a 3-phase AC
// motor from a DC supply: instead of outputting a clean sine wave directly
// (DACs can't source the current a motor needs), each phase leg's switch
// is turned on/off very fast (the "carrier" frequency - fixed at 20kHz,
// see SPWM_CARRIER_HZ in spwm.c) with a duty cycle that itself traces out
// a sine wave over time (the much slower "output"/"sine" frequency you set
// via spwm_set_frequency() - the thing the motor actually sees as its
// supply frequency). Low-pass filtering by the motor's own winding
// inductance (or an external LC filter) smooths that fast switching back
// into something close to a real sine wave at the output frequency.
//
// "3-phase, 120 degrees apart" means phases A/B/C each carry the same sine
// shape but time-shifted by a third of a cycle - the standard way to drive
// a 3-phase motor so it produces smooth rotating torque rather than pulsing.

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Starts a 3-phase sinusoidal PWM (SPWM) output: one modulated PWM
 *        channel per phase leg (A/B/C, 120 degrees apart), sharing a single
 *        fixed-frequency carrier. No complementary high/low pair or
 *        dead-time yet - that's the later 6-channel extension once the
 *        low-side gate-driver pins are wired.
 *
 * @param gpio_a/b/c        Phase A/B/C output GPIOs.
 * @param initial_freq_hz   Starting sine (output) frequency, clamped to [1, 100] Hz.
 */
esp_err_t spwm_init(int gpio_a, int gpio_b, int gpio_c, float initial_freq_hz);

/** @brief Updates the sine frequency, clamped to [1, 100] Hz. Returns the applied value. */
float spwm_set_frequency(float freq_hz);

/** @brief Returns the currently applied sine frequency. */
float spwm_get_frequency(void);

#ifdef __cplusplus
}
#endif
