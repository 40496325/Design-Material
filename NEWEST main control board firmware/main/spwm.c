#include "spwm.h"
#include "driver/mcpwm_prelude.h"
#include "esp_attr.h"
#include "esp_log.h"
#include <math.h>

static const char *TAG = "spwm";

#define SPWM_CARRIER_HZ   20000                                    // switching frequency - fixed, inaudible
#define SPWM_TIMER_RES_HZ 40000000                                 // 40MHz timebase (160MHz PLL / 4)
#define SPWM_PERIOD_TICKS (SPWM_TIMER_RES_HZ / SPWM_CARRIER_HZ)     // 2000 ticks/period
#define SPWM_MODULATION_INDEX 0.9f                                 // fixed amplitude (0-1); not currently runtime-adjustable

#define SPWM_MIN_FREQ_HZ 1.0f
#define SPWM_MAX_FREQ_HZ 100.0f

#define SINE_LUT_BITS 8
#define SINE_LUT_SIZE (1u << SINE_LUT_BITS)  // 256 entries, one full sine cycle

// 120 degrees expressed in Q32 phase units (a full circle = 2^32).
#define PHASE_120_OFFSET ((uint32_t)(4294967296.0 / 3.0))

static mcpwm_timer_handle_t s_timer;
static mcpwm_cmpr_handle_t s_cmp[3];

// Precomputed compare-tick values for one full sine cycle
// (0.5 + 0.5*modulation_index*sin), so the timer ISR is pure integer table
// lookups at 20kHz - no floating point or trig in the hot path.
static uint32_t s_duty_lut[SINE_LUT_SIZE];

// s_phase_accum/s_phase_step implement a phase accumulator, aka a DDS
// (Direct Digital Synthesis) oscillator - the standard trick for
// generating a variable-frequency periodic signal from a fixed-rate timer
// without redoing any trig at runtime. The idea:
//   - A uint32_t "phase" value represents one full sine cycle as the range
//     [0, 2^32) - "Q32 phase units". It wraps around for free on overflow.
//   - Every carrier tick (on_timer_empty(), 20,000 times/sec), the
//     accumulator advances by a fixed "step" size: s_phase_accum += s_phase_step.
//   - The top SINE_LUT_BITS bits of the current phase select which of the
//     256 precomputed LUT entries to output "right now" - i.e. the phase
//     accumulator's upper bits are just a sine-cycle position in [0, 256).
//   - A bigger step means the phase wraps around (completes one sine
//     cycle) faster, i.e. a higher output frequency - see
//     freq_to_phase_step() below for exactly how step size maps to Hz.
// PHASE_120_OFFSET is added to the shared phase before indexing the LUT for
// phases B and C, which is what keeps all three outputs locked to the same
// frequency while staying 120 degrees apart - there's deliberately only one
// oscillator (s_phase_accum), not three independent ones.
static volatile uint32_t s_phase_step;
static volatile uint32_t s_phase_accum;
static float s_current_freq_hz;

static uint32_t freq_to_phase_step(float freq_hz)
{
    return (uint32_t)((double)freq_hz / SPWM_CARRIER_HZ * 4294967296.0);
}

static float clamp_freq(float freq_hz)
{
    if (freq_hz < SPWM_MIN_FREQ_HZ) {
        return SPWM_MIN_FREQ_HZ;
    }
    if (freq_hz > SPWM_MAX_FREQ_HZ) {
        return SPWM_MAX_FREQ_HZ;
    }
    return freq_hz;
}

// Runs once per carrier period (20,000/s). Advances the phase accumulator
// and pushes the three 120-degree-apart duty values straight from the LUT -
// deliberately no floating point or function calls beyond the MCPWM setters.
static bool IRAM_ATTR on_timer_empty(mcpwm_timer_handle_t timer, const mcpwm_timer_event_data_t *edata, void *user_ctx)
{
    uint32_t phase = s_phase_accum;
    s_phase_accum = phase + s_phase_step;

    uint32_t idx_a = phase >> (32 - SINE_LUT_BITS);
    uint32_t idx_b = (phase + PHASE_120_OFFSET) >> (32 - SINE_LUT_BITS);
    uint32_t idx_c = (phase + 2 * PHASE_120_OFFSET) >> (32 - SINE_LUT_BITS);

    mcpwm_comparator_set_compare_value(s_cmp[0], s_duty_lut[idx_a]);
    mcpwm_comparator_set_compare_value(s_cmp[1], s_duty_lut[idx_b]);
    mcpwm_comparator_set_compare_value(s_cmp[2], s_duty_lut[idx_c]);

    return false;
}

esp_err_t spwm_init(int gpio_a, int gpio_b, int gpio_c, float initial_freq_hz)
{
    for (unsigned i = 0; i < SINE_LUT_SIZE; i++) {
        float angle = 2.0f * (float)M_PI * (float)i / SINE_LUT_SIZE;
        float duty = 0.5f + 0.5f * SPWM_MODULATION_INDEX * sinf(angle);
        s_duty_lut[i] = (uint32_t)(duty * SPWM_PERIOD_TICKS);
    }

    mcpwm_timer_config_t timer_config = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = SPWM_TIMER_RES_HZ,
        .period_ticks = SPWM_PERIOD_TICKS,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    esp_err_t err = mcpwm_new_timer(&timer_config, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer create failed (%s)", esp_err_to_name(err));
        return err;
    }

    int gpios[3] = {gpio_a, gpio_b, gpio_c};
    for (int i = 0; i < 3; i++) {
        mcpwm_operator_config_t oper_config = { .group_id = 0 };
        mcpwm_oper_handle_t oper;
        err = mcpwm_new_operator(&oper_config, &oper);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "operator %d create failed (%s)", i, esp_err_to_name(err));
            return err;
        }

        err = mcpwm_operator_connect_timer(oper, s_timer);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "operator %d connect failed (%s)", i, esp_err_to_name(err));
            return err;
        }

        mcpwm_comparator_config_t cmp_config = { .flags.update_cmp_on_tez = true };
        err = mcpwm_new_comparator(oper, &cmp_config, &s_cmp[i]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "comparator %d create failed (%s)", i, esp_err_to_name(err));
            return err;
        }
        err = mcpwm_comparator_set_compare_value(s_cmp[i], s_duty_lut[0]);
        if (err != ESP_OK) {
            return err;
        }

        mcpwm_generator_config_t gen_config = { .gen_gpio_num = gpios[i] };
        mcpwm_gen_handle_t gen;
        err = mcpwm_new_generator(oper, &gen_config, &gen);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "generator %d create failed on GPIO %d (%s)", i, gpios[i], esp_err_to_name(err));
            return err;
        }

        // Standard edge-aligned PWM: high at the start of the period, low
        // once the count reaches the (sine-modulated) compare value.
        err = mcpwm_generator_set_action_on_timer_event(gen,
                MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
        if (err != ESP_OK) {
            return err;
        }
        err = mcpwm_generator_set_action_on_compare_event(gen,
                MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_cmp[i], MCPWM_GEN_ACTION_LOW));
        if (err != ESP_OK) {
            return err;
        }
    }

    mcpwm_timer_event_callbacks_t cbs = { .on_empty = on_timer_empty };
    err = mcpwm_timer_register_event_callbacks(s_timer, &cbs, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "callback register failed (%s)", esp_err_to_name(err));
        return err;
    }

    s_current_freq_hz = clamp_freq(initial_freq_hz);
    s_phase_step = freq_to_phase_step(s_current_freq_hz);

    err = mcpwm_timer_enable(s_timer);
    if (err != ESP_OK) {
        return err;
    }
    err = mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "started: %d Hz carrier, %.1f Hz sine, GPIOs %d/%d/%d",
             SPWM_CARRIER_HZ, s_current_freq_hz, gpio_a, gpio_b, gpio_c);
    return ESP_OK;
}

float spwm_set_frequency(float freq_hz)
{
    freq_hz = clamp_freq(freq_hz);
    s_phase_step = freq_to_phase_step(freq_hz);
    s_current_freq_hz = freq_hz;
    return freq_hz;
}

float spwm_get_frequency(void)
{
    return s_current_freq_hz;
}
