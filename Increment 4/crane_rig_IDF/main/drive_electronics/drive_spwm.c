// drive_spwm.c - see drive_spwm.h
#include "drive_spwm.h"

#include <math.h>

#include "driver/gpio.h"
#include "driver/mcpwm_prelude.h"
#include "esp_attr.h"
#include "esp_log.h"

static const char *TAG = "drive_spwm";

#define TIMER_RES_HZ 40000000u      // 160 MHz / 4: 25 ns per tick
#define LUT_BITS 8
#define LUT_SIZE (1u << LUT_BITS)   // 256 points per sine cycle
#define PH120 ((uint32_t)(4294967296.0 / 3.0))
#define PH240 ((uint32_t)(2.0 * 4294967296.0 / 3.0))
#define FREQ_MIN_HZ 0.5f
#define FREQ_MAX_HZ 100.0f

static drive_spwm_cfg_t s_cfg;
static mcpwm_timer_handle_t s_timer;
static mcpwm_cmpr_handle_t s_cmp[3];
static mcpwm_gen_handle_t s_gen[3];

// sin(2*pi*i/256) in Q15 - read by the ISR, so kept in internal RAM.
static DRAM_ATTR int16_t s_lut[LUT_SIZE];

// ISR state (written by the drive task, read by the ISR every 100 us).
static volatile drive_spwm_mode_t s_mode = DRIVE_SPWM_IDLE;
static volatile uint32_t s_acc;       // phase accumulator, 2^32 = one sine cycle
static volatile uint32_t s_step;      // added every carrier period
static volatile int32_t s_amp;        // m * half, in ticks
static volatile int32_t s_half;       // period / 2
static volatile int32_t s_cmin, s_cmax;
static volatile bool s_rev;
static volatile uint32_t s_isr_count;

static uint32_t s_period;
static float s_freq, s_m, s_fixed_duty;
static uint8_t s_mask;

static inline __attribute__((always_inline)) uint32_t sine_cmp(uint32_t phase)
{
    int32_t c = s_half + ((s_amp * (int32_t)s_lut[phase >> (32 - LUT_BITS)]) >> 15);
    if (c < s_cmin) c = s_cmin;
    if (c > s_cmax) c = s_cmax;
    return (uint32_t)c;
}

static bool IRAM_ATTR on_timer_empty(mcpwm_timer_handle_t timer, const mcpwm_timer_event_data_t *edata, void *ctx)
{
    (void)timer;
    (void)edata;
    (void)ctx;
    s_isr_count++;
    if (s_mode != DRIVE_SPWM_SINE) return false;

    const uint32_t ph = s_acc;
    s_acc = ph + s_step;
    // Forward A-B-C: B lags A by 120 deg (theta - 120 = theta + 240), C lags by 240.
    // Reverse A-C-B: B and C swap places.
    const uint32_t off_b = s_rev ? PH120 : PH240;
    const uint32_t off_c = s_rev ? PH240 : PH120;
    mcpwm_comparator_set_compare_value(s_cmp[0], sine_cmp(ph));
    mcpwm_comparator_set_compare_value(s_cmp[1], sine_cmp(ph + off_b));
    mcpwm_comparator_set_compare_value(s_cmp[2], sine_cmp(ph + off_c));
    return false;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static uint32_t freq_to_step(float f)
{
    return (uint32_t)((double)f / (double)s_cfg.carrier_hz * 4294967296.0);
}

static void force_all_idle(void)
{
    for (int i = 0; i < 3; i++) mcpwm_generator_set_force_level(s_gen[i], s_cfg.idle_in_level, true);
}

esp_err_t drive_spwm_init(const drive_spwm_cfg_t *cfg)
{
    s_cfg = *cfg;
    if (s_cfg.carrier_hz < 1000 || s_cfg.carrier_hz > 40000) {
        ESP_LOGE(TAG, "carrier %u Hz out of range (1-40 kHz)", (unsigned)s_cfg.carrier_hz);
        return ESP_ERR_INVALID_ARG;
    }
    s_period = TIMER_RES_HZ / s_cfg.carrier_hz;                 // 10 kHz -> 4000 ticks
    s_half = (int32_t)(s_period / 2);
    s_cmin = (int32_t)lroundf(clampf(s_cfg.duty_min, 0.01f, 0.49f) * (float)s_period);
    s_cmax = (int32_t)lroundf(clampf(s_cfg.duty_max, 0.51f, 0.99f) * (float)s_period);
    for (unsigned i = 0; i < LUT_SIZE; i++) {
        s_lut[i] = (int16_t)lroundf(32767.0f * sinf(2.0f * (float)M_PI * (float)i / (float)LUT_SIZE));
    }

    mcpwm_timer_config_t tcfg = {
        .group_id = 0,
        .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
        .resolution_hz = TIMER_RES_HZ,
        .period_ticks = s_period,
        .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
    };
    esp_err_t err = mcpwm_new_timer(&tcfg, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "timer create failed (%s)", esp_err_to_name(err));
        return err;
    }

    const int gpios[3] = {s_cfg.gpio_a, s_cfg.gpio_b, s_cfg.gpio_c};
    for (int i = 0; i < 3; i++) {
        mcpwm_operator_config_t ocfg = {.group_id = 0};
        mcpwm_oper_handle_t oper;
        if ((err = mcpwm_new_operator(&ocfg, &oper)) != ESP_OK) return err;
        if ((err = mcpwm_operator_connect_timer(oper, s_timer)) != ESP_OK) return err;

        mcpwm_comparator_config_t ccfg = {.flags.update_cmp_on_tez = true};
        if ((err = mcpwm_new_comparator(oper, &ccfg, &s_cmp[i])) != ESP_OK) return err;
        if ((err = mcpwm_comparator_set_compare_value(s_cmp[i], (uint32_t)s_half)) != ESP_OK) return err;

        mcpwm_generator_config_t gcfg = {.gen_gpio_num = gpios[i]};
        gcfg.flags.invert_pwm = s_cfg.opto_inverts;   // pin = NOT(IN) because the 4N25 inverts again
        if ((err = mcpwm_new_generator(oper, &gcfg, &s_gen[i])) != ESP_OK) {
            ESP_LOGE(TAG, "generator %d on GPIO%d failed (%s)", i, gpios[i], esp_err_to_name(err));
            return err;
        }
        // Hold the idle level straight away, before the timer runs.
        mcpwm_generator_set_force_level(s_gen[i], s_cfg.idle_in_level, true);

        // IN domain: HIGH from the start of the period until the count reaches compare.
        err = mcpwm_generator_set_action_on_timer_event(s_gen[i],
              MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH));
        if (err != ESP_OK) return err;
        err = mcpwm_generator_set_action_on_compare_event(s_gen[i],
              MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, s_cmp[i], MCPWM_GEN_ACTION_LOW));
        if (err != ESP_OK) return err;

        if (s_cfg.max_drive_strength) gpio_set_drive_capability((gpio_num_t)gpios[i], GPIO_DRIVE_CAP_3);
    }

    mcpwm_timer_event_callbacks_t cbs = {.on_empty = on_timer_empty};
    if ((err = mcpwm_timer_register_event_callbacks(s_timer, &cbs, NULL)) != ESP_OK) return err;
    if ((err = mcpwm_timer_enable(s_timer)) != ESP_OK) return err;
    if ((err = mcpwm_timer_start_stop(s_timer, MCPWM_TIMER_START_NO_STOP)) != ESP_OK) return err;

    ESP_LOGI(TAG, "ready: %u Hz carrier (%u ticks @ 40 MHz), GPIO%d/%d/%d = A/B/C, opto %s, "
                  "idle IN=%d, SINE duty %.0f-%.0f %%, outputs IDLE",
             (unsigned)s_cfg.carrier_hz, (unsigned)s_period, gpios[0], gpios[1], gpios[2],
             s_cfg.opto_inverts ? "inverting (pin inverted)" : "non-inverting", s_cfg.idle_in_level,
             100.0f * (float)s_cmin / (float)s_period, 100.0f * (float)s_cmax / (float)s_period);
    return ESP_OK;
}

void drive_spwm_idle(void)
{
    if (!s_timer) return;
    force_all_idle();                 // pins first ...
    s_mode = DRIVE_SPWM_IDLE;         // ... then stop the ISR updating compares
    s_freq = 0.0f;
    s_m = 0.0f;
    s_mask = 0;
}

esp_err_t drive_spwm_start_sine(float freq_hz, float m, bool reverse)
{
    if (!s_timer) return ESP_ERR_INVALID_STATE;
    if (s_mode == DRIVE_SPWM_SINE) {
        if (reverse != s_rev) return ESP_ERR_INVALID_STATE;   // EEII 1: stop first, then swap
        drive_spwm_set_sine(freq_hz, m);
        return ESP_OK;
    }
    force_all_idle();
    s_mode = DRIVE_SPWM_IDLE;
    s_freq = clampf(freq_hz, FREQ_MIN_HZ, FREQ_MAX_HZ);
    s_m = clampf(m, 0.0f, 1.0f);
    s_step = freq_to_step(s_freq);
    s_amp = (int32_t)lroundf(s_m * (float)s_half);
    s_rev = reverse;
    s_acc = 0;
    mcpwm_comparator_set_compare_value(s_cmp[0], sine_cmp(0));
    mcpwm_comparator_set_compare_value(s_cmp[1], sine_cmp(reverse ? PH120 : PH240));
    mcpwm_comparator_set_compare_value(s_cmp[2], sine_cmp(reverse ? PH240 : PH120));
    s_mode = DRIVE_SPWM_SINE;
    s_mask = DRIVE_PHASE_ALL;
    for (int i = 0; i < 3; i++) mcpwm_generator_set_force_level(s_gen[i], -1, true);   // release
    return ESP_OK;
}

void drive_spwm_set_sine(float freq_hz, float m)
{
    if (s_mode != DRIVE_SPWM_SINE) return;
    s_freq = clampf(freq_hz, FREQ_MIN_HZ, FREQ_MAX_HZ);
    s_m = clampf(m, 0.0f, 1.0f);
    s_step = freq_to_step(s_freq);
    s_amp = (int32_t)lroundf(s_m * (float)s_half);
}

esp_err_t drive_spwm_fixed(uint8_t phase_mask, float pin_duty)
{
    if (!s_timer) return ESP_ERR_INVALID_STATE;
    phase_mask &= DRIVE_PHASE_ALL;
    pin_duty = clampf(pin_duty, 0.01f, 0.99f);
    const float in_duty = s_cfg.opto_inverts ? 1.0f - pin_duty : pin_duty;
    uint32_t cmp = (uint32_t)lroundf(in_duty * (float)s_period);
    if (cmp < 1) cmp = 1;
    if (cmp > s_period - 1) cmp = s_period - 1;

    force_all_idle();
    s_mode = DRIVE_SPWM_IDLE;
    for (int i = 0; i < 3; i++) mcpwm_comparator_set_compare_value(s_cmp[i], cmp);
    s_mode = DRIVE_SPWM_FIXED;
    s_mask = phase_mask;
    s_fixed_duty = pin_duty;
    s_freq = 0.0f;
    for (int i = 0; i < 3; i++) {
        if (phase_mask & (1u << i)) mcpwm_generator_set_force_level(s_gen[i], -1, true);
    }
    return ESP_OK;
}

drive_spwm_mode_t drive_spwm_mode(void) { return s_mode; }
float drive_spwm_freq(void) { return s_mode == DRIVE_SPWM_SINE ? s_freq : 0.0f; }
float drive_spwm_m(void) { return s_mode == DRIVE_SPWM_SINE ? s_m : 0.0f; }
bool drive_spwm_reversed(void) { return s_rev; }
uint8_t drive_spwm_phase_mask(void) { return s_mode == DRIVE_SPWM_IDLE ? 0 : s_mask; }
float drive_spwm_fixed_duty(void) { return s_fixed_duty; }
uint32_t drive_spwm_period_ticks(void) { return s_period; }
uint32_t drive_spwm_isr_count(void) { return s_isr_count; }
