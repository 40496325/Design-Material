// =============================================================================
// trolley_io.h - MCTR 1 lateral drive hardware (ICD IF1.1 + IF1.2)
// =============================================================================
// Added with the trolley firmware (Increment 4, 17 Nov 2026).
//
// Pins: ONLY those in "MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx"
// (the final pinout for every package), section 3, MCTR 1:
//
//   J7.9  (IO_A)  LAT_PWM_ENA  -> GPIO18, LEDC PWM (1-10 kHz) to HW-095 ENA
//   J7.12 (IO_D)  ENC_A        <- GPIO21, PCNT pulse counter (stepped down 5 V -> 3.3 V)
//   J4.4  (GPB1)  MOT_IN1      -> MCP23017 port B
//   J4.5  (GPB2)  MOT_IN2      -> MCP23017 port B
//   J4.6  (GPB3)  MOT_IN3      -> copy of IN1 (only used if the 2nd bridge is wired to J4)
//   J4.7  (GPB4)  MOT_IN4      -> copy of IN2
//   J4.2 +5 V / J4.1 GND       power for the HW-095 logic and the encoder (not firmware)
//   J7.11 (IO_C)  ENC_B        not used (single-channel encoder)
//
// HW-095 = L298N. Truth table (ST L298 datasheet), EN = ENA PWM:
//   EN high, IN1 H / IN2 L  -> forward ("+", trolley_cmd > 0)
//   EN high, IN1 L / IN2 H  -> reverse ("-")
//   EN high, IN1 = IN2      -> fast stop (motor terminals shorted = braking)
//   EN low                  -> free running (coast)
// The PWM on EN therefore chops between "drive" and "coast".
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int pwm_gpio;          // J7.9 = GPIO18
    uint32_t pwm_hz;       // ENA PWM frequency (ICD / wiring doc: 1-10 kHz)
    int in1_bit, in2_bit;  // MCP23017 port B bits (J4.4 = GPB1, J4.5 = GPB2)
    bool mirror_in34;      // also drive IN3 / IN4 as copies of IN1 / IN2
    int in3_bit, in4_bit;  // J4.6 = GPB3, J4.7 = GPB4
    int enc_gpio;          // J7.12 = GPIO21
    bool enc_pullup;       // internal pull-up on the encoder input
    uint32_t enc_glitch_ns;// PCNT glitch filter: pulses shorter than this are ignored
} trolley_io_cfg_t;

typedef enum {
    TROLLEY_IN_COAST = 0,  // IN1 = IN2 = LOW (with EN low: free running)
    TROLLEY_IN_FWD = 1,    // IN1 HIGH, IN2 LOW
    TROLLEY_IN_REV = 2,    // IN1 LOW,  IN2 HIGH
    TROLLEY_IN_BRAKE = 3,  // IN1 = IN2 = HIGH (with EN high: braking)
} trolley_in_t;

esp_err_t trolley_io_init(const trolley_io_cfg_t *cfg);

/** ENA duty 0..1 (0 = EN held LOW). */
void trolley_io_set_duty(float duty);
float trolley_io_duty(void);

/** IN1-IN4 in one MCP23017 write. */
esp_err_t trolley_io_set_in(trolley_in_t in);
trolley_in_t trolley_io_in(void);
const char *trolley_io_in_name(trolley_in_t in);

/** Encoder edges counted since the last call (rising edges on ENC_A, always
 *  >= 0: a single channel cannot tell direction). Call every 10 ms. */
int32_t trolley_io_encoder_delta(void);

/** Total rising edges seen since boot (for the console). */
uint32_t trolley_io_encoder_total(void);

bool trolley_io_encoder_ready(void);
uint32_t trolley_io_pwm_hz(void);

#ifdef __cplusplus
}
#endif
