// trolley_io.c - see trolley_io.h
#include "trolley_io.h"

#include <math.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/pulse_cnt.h"
#include "esp_log.h"

#include "mcp_shared.h"

static const char *TAG = "trolley_io";

#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_0       // first LEDC user on this board (status LED uses RMT)
#define LEDC_CHAN LEDC_CHANNEL_0
#define LEDC_RES LEDC_TIMER_10_BIT    // 1024 steps: 0.1 % duty resolution
#define LEDC_MAX ((1u << 10) - 1u)

// The PCNT counter is 16-bit signed. It only counts up here; when it reaches
// PCNT_HIGH it restarts at 0, and the 10 ms poll adds the wrap back. At the
// fastest plausible encoder rate (~10 kHz) that is 100 edges per poll, far
// below 30 000, so a wrap can never be mistaken.
#define PCNT_HIGH 30000

static trolley_io_cfg_t s_cfg;
static bool s_pwm_ok, s_enc_ok;
static float s_duty;
static trolley_in_t s_in = TROLLEY_IN_COAST;
static pcnt_unit_handle_t s_pcnt;
static int s_prev_count;
static uint32_t s_total;

esp_err_t trolley_io_init(const trolley_io_cfg_t *cfg)
{
    s_cfg = *cfg;
    esp_err_t first_err = ESP_OK;

    // 1. Direction pins first: IN1-IN4 LOW (coast) before ENA can ever go high.
    if (mcp_shared_ok()) {
        mcp_shared_config_output(MCP23017_PORT_B, cfg->in1_bit, 0);
        mcp_shared_config_output(MCP23017_PORT_B, cfg->in2_bit, 0);
        if (cfg->mirror_in34) {
            mcp_shared_config_output(MCP23017_PORT_B, cfg->in3_bit, 0);
            mcp_shared_config_output(MCP23017_PORT_B, cfg->in4_bit, 0);
        }
    } else {
        ESP_LOGE(TAG, "MCP23017 missing: IN1-IN4 (J4.4-J4.7) NOT driven - the motor cannot be turned on");
    }

    // 2. ENA PWM on J7.9, created at 0 % duty.
    ledc_timer_config_t tcfg = {
        .speed_mode = LEDC_MODE,
        .duty_resolution = LEDC_RES,
        .timer_num = LEDC_TIMER,
        .freq_hz = cfg->pwm_hz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&tcfg);
    if (err == ESP_OK) {
        ledc_channel_config_t ccfg = {
            .gpio_num = cfg->pwm_gpio,
            .speed_mode = LEDC_MODE,
            .channel = LEDC_CHAN,
            .timer_sel = LEDC_TIMER,
            .duty = 0,
            .hpoint = 0,
        };
        err = ledc_channel_config(&ccfg);
    }
    if (err == ESP_OK) {
        s_pwm_ok = true;
    } else {
        ESP_LOGE(TAG, "ENA PWM on GPIO%d failed (%s)", cfg->pwm_gpio, esp_err_to_name(err));
        first_err = err;
    }

    // 3. Encoder on J7.12: count rising edges only (ICD Table 15).
    pcnt_unit_config_t ucfg = {
        .clk_src = PCNT_CLK_SRC_DEFAULT,
        .low_limit = -1,
        .high_limit = PCNT_HIGH,
    };
    err = pcnt_new_unit(&ucfg, &s_pcnt);
    if (err == ESP_OK) {
        pcnt_glitch_filter_config_t fcfg = {.max_glitch_ns = cfg->enc_glitch_ns};
        pcnt_unit_set_glitch_filter(s_pcnt, &fcfg);
        pcnt_chan_config_t chcfg = {.edge_gpio_num = cfg->enc_gpio, .level_gpio_num = -1};
        pcnt_channel_handle_t chan;
        err = pcnt_new_channel(s_pcnt, &chcfg, &chan);
        if (err == ESP_OK) {
            pcnt_channel_set_edge_action(chan, PCNT_CHANNEL_EDGE_ACTION_INCREASE, PCNT_CHANNEL_EDGE_ACTION_HOLD);
            gpio_set_pull_mode((gpio_num_t)cfg->enc_gpio, cfg->enc_pullup ? GPIO_PULLUP_ONLY : GPIO_FLOATING);
            err = pcnt_unit_enable(s_pcnt);
        }
        if (err == ESP_OK) err = pcnt_unit_clear_count(s_pcnt);
        if (err == ESP_OK) err = pcnt_unit_start(s_pcnt);
    }
    if (err == ESP_OK) {
        s_enc_ok = true;
    } else {
        ESP_LOGE(TAG, "encoder PCNT on GPIO%d failed (%s)", cfg->enc_gpio, esp_err_to_name(err));
        if (first_err == ESP_OK) first_err = err;
    }

    ESP_LOGI(TAG, "ENA PWM GPIO%d (J7.9) %u Hz %s, IN1/IN2 GPB%d/GPB%d (J4.4/J4.5)%s, encoder GPIO%d (J7.12) %s, "
                  "pull-up %s, filter %u ns",
             cfg->pwm_gpio, (unsigned)cfg->pwm_hz, s_pwm_ok ? "ok" : "FAILED", cfg->in1_bit, cfg->in2_bit,
             cfg->mirror_in34 ? ", IN3/IN4 = copy on GPB3/GPB4 (J4.6/J4.7)" : "", cfg->enc_gpio,
             s_enc_ok ? "ok" : "FAILED", cfg->enc_pullup ? "on" : "off", (unsigned)cfg->enc_glitch_ns);
    return first_err;
}

void trolley_io_set_duty(float duty)
{
    if (duty < 0.0f) duty = 0.0f;
    if (duty > 1.0f) duty = 1.0f;
    if (fabsf(duty - s_duty) < 0.0005f) return;
    s_duty = duty;
    if (!s_pwm_ok) return;
    ledc_set_duty(LEDC_MODE, LEDC_CHAN, (uint32_t)lroundf(duty * (float)LEDC_MAX));
    ledc_update_duty(LEDC_MODE, LEDC_CHAN);
}

float trolley_io_duty(void) { return s_duty; }

esp_err_t trolley_io_set_in(trolley_in_t in)
{
    if (in == s_in) return ESP_OK;
    const int in1 = (in == TROLLEY_IN_FWD || in == TROLLEY_IN_BRAKE) ? 1 : 0;
    const int in2 = (in == TROLLEY_IN_REV || in == TROLLEY_IN_BRAKE) ? 1 : 0;
    uint8_t mask = (uint8_t)((1u << s_cfg.in1_bit) | (1u << s_cfg.in2_bit));
    uint8_t lv = (uint8_t)((in1 << s_cfg.in1_bit) | (in2 << s_cfg.in2_bit));
    if (s_cfg.mirror_in34) {
        mask |= (uint8_t)((1u << s_cfg.in3_bit) | (1u << s_cfg.in4_bit));
        lv |= (uint8_t)((in1 << s_cfg.in3_bit) | (in2 << s_cfg.in4_bit));
    }
    esp_err_t err = mcp_shared_write_mask(MCP23017_PORT_B, mask, lv);
    if (err == ESP_OK || !mcp_shared_ok()) s_in = in;
    return err;
}

trolley_in_t trolley_io_in(void) { return s_in; }

const char *trolley_io_in_name(trolley_in_t in)
{
    switch (in) {
    case TROLLEY_IN_FWD: return "FWD";
    case TROLLEY_IN_REV: return "REV";
    case TROLLEY_IN_BRAKE: return "BRAKE";
    default: return "COAST";
    }
}

int32_t trolley_io_encoder_delta(void)
{
    if (!s_enc_ok) return 0;
    int now = 0;
    if (pcnt_unit_get_count(s_pcnt, &now) != ESP_OK) return 0;
    int d = now - s_prev_count;
    if (d < 0) d += PCNT_HIGH;   // the counter restarted at 0 after reaching PCNT_HIGH
    s_prev_count = now;
    s_total += (uint32_t)d;
    return d;
}

uint32_t trolley_io_encoder_total(void) { return s_total; }
bool trolley_io_encoder_ready(void) { return s_enc_ok; }
uint32_t trolley_io_pwm_hz(void) { return s_cfg.pwm_hz; }
