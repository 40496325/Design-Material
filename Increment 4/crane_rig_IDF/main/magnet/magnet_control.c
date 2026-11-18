// magnet_control.c - see magnet_control.h
#include "magnet_control.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "loadcell.h"
#include "mcp_shared.h"

static const char *TAG = "magnet";

#define TICK_MS 20
#define REQ_SLOTS 4
#define LEDC_MODE LEDC_LOW_SPEED_MODE
#define LEDC_TIMER LEDC_TIMER_1       // timer 0 / channel 0 belong to the trolley ENA PWM
#define LEDC_CHAN LEDC_CHANNEL_1
#define LEDC_RES LEDC_TIMER_10_BIT    // 15 kHz x 1024 = 15.4 MHz, well inside the 80 MHz clock
#define LEDC_MAX ((1u << 10) - 1u)
#define ADS_FULL_SCALE 8388608.0f     // 2^23: single-ended code at V = VREF

typedef enum { ST_OFF, ST_ON, ST_TEST } st_t;
static const char *const ST_NAME[] = {"OFF", "ON", "TEST"};

typedef enum { REQ_NONE, REQ_PWM, REQ_ON, REQ_OFF, REQ_LOGIC, REQ_ZERO, REQ_CAL, REQ_THRESHOLD, REQ_RESET, REQ_STOP } req_type_t;
typedef struct {
    req_type_t type;
    float value;
    int flag;
} req_t;

static struct {
    magnet_cfg_t cfg;
    bool present, pwm_ok;
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_buf;

    // Written by the radio task (under lock)
    bool have_cmd;
    uint8_t cmd_state;
    bool reset_req;
    req_t req[REQ_SLOTS];
    uint8_t req_head, req_count;

    // Magnet task
    st_t st;
    bool engaged;            // commanded on (dashboard or test ON)
    bool logic;              // MAG_LOGIC level is "on"
    float duty;              // applied duty %
    int64_t engaged_ms;      // when the engage started
    int64_t released_ms;     // when the last release happened
    bool lead_pending;       // MAG_LOGIC set, PWM still waiting for logic_lead_ms
    float zero_v, sens;      // live calibration (start from cfg, changed by ZERO / CAL / auto-zero)
    float threshold;
    uint32_t aux_seen;
    bool cur_valid;
    bool sensor_fault;
    bool release_high, over_current;
    float sensor_v, current_a;
    uint8_t status;
    bool fault;
} m;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// -----------------------------------------------------------------------------
// Outputs
// -----------------------------------------------------------------------------
static void set_duty(float pct)
{
    if (pct < 0.0f) pct = 0.0f;
    if (pct > 100.0f) pct = 100.0f;
    m.duty = pct;
    if (!m.pwm_ok) return;
    ledc_set_duty(LEDC_MODE, LEDC_CHAN, (uint32_t)lroundf(pct / 100.0f * (float)LEDC_MAX));
    ledc_update_duty(LEDC_MODE, LEDC_CHAN);
}

static void set_logic(bool on)
{
    m.logic = on;
    const int level = on ? (m.cfg.logic_on_high ? 1 : 0) : (m.cfg.logic_on_high ? 0 : 1);
    if (mcp_shared_write(MCP23017_PORT_A, m.cfg.logic_bit, level) != ESP_OK && mcp_shared_ok()) {
        printf("RIG,WARN,MAGNET,MAG_LOGIC (J3.6) write failed\n");
    }
}

// Engage: MAG_LOGIC first, PWM to on_duty after logic_lead_ms (done in the task).
static void engage(void)
{
    if (m.engaged) return;
    m.engaged = true;
    m.engaged_ms = now_ms();
    m.fault = false;
    set_logic(true);
    if (m.cfg.logic_lead_ms == 0) {
        set_duty(m.cfg.on_duty_pct);
        m.lead_pending = false;
    } else {
        m.lead_pending = true;
    }
    printf("RIG,MAGNET,ENGAGE,MAG_LOGIC HIGH, PWM %.0f %% at %u Hz on J7.5\n", m.cfg.on_duty_pct, (unsigned)m.cfg.pwm_hz);
}

// Release: PWM to release_duty first, then MAG_LOGIC low.
static void release(void)
{
    if (!m.engaged && !m.logic && m.duty == m.cfg.release_duty_pct) return;
    const bool was = m.engaged;
    m.engaged = false;
    m.lead_pending = false;
    m.released_ms = now_ms();
    m.fault = false;
    set_duty(m.cfg.release_duty_pct);
    set_logic(false);
    if (was) printf("RIG,MAGNET,RELEASE,PWM %.0f %%, MAG_LOGIC LOW\n", m.cfg.release_duty_pct);
}

static void go(st_t st)
{
    if (st == m.st) return;
    printf("RIG,MAGNET,STATE,%s->%s,duty=%.1f,logic=%d\n", ST_NAME[m.st], ST_NAME[st], m.duty, m.logic);
    m.st = st;
}

// -----------------------------------------------------------------------------
// Current sensor
// -----------------------------------------------------------------------------
static void update_current(int64_t t)
{
    if (!m.cfg.sense_enabled) {
        m.cur_valid = false;
        return;
    }
    int32_t code;
    int64_t when;
    uint32_t count;
    const bool ok = loadcell_get_aux(&code, &when, &count);
    if (!ok) {
        if ((esp_timer_get_time() - when) > 3LL * m.cfg.aux_period_ms * 1000) m.cur_valid = false;
        return;
    }
    if (count == m.aux_seen) {
        if ((esp_timer_get_time() - when) > 3LL * m.cfg.aux_period_ms * 1000) m.cur_valid = false;   // stale
        return;
    }
    m.aux_seen = count;
    m.sensor_v = (float)code / ADS_FULL_SCALE * m.cfg.avdd_v;
    const bool plausible = m.sensor_v >= m.cfg.sensor_min_v && m.sensor_v <= m.cfg.sensor_max_v;
    if (!plausible) {
        if (!m.sensor_fault) {
            printf("RIG,MAGNET,SENSOR,J6.8 reads %.3f V (outside %.2f-%.2f V) - sensor unplugged / unpowered?\n",
                   m.sensor_v, m.cfg.sensor_min_v, m.cfg.sensor_max_v);
        }
        m.sensor_fault = true;
        m.cur_valid = false;
        m.current_a = 0.0f;
        return;
    }
    if (m.sensor_fault) printf("RIG,MAGNET,SENSOR,J6.8 back in range (%.3f V)\n", m.sensor_v);
    m.sensor_fault = false;
    m.cur_valid = true;

    // Auto-zero: the magnet has been released (or never engaged) long enough.
    if (m.cfg.auto_zero && !m.engaged && m.st != ST_TEST && (t - m.released_ms) >= (int64_t)m.cfg.auto_zero_after_ms &&
        fabsf(m.sensor_v - m.cfg.zero_v) < m.cfg.auto_zero_window_v) {   // only small drift: a real release current stays visible
        m.zero_v += 0.2f * (m.sensor_v - m.zero_v);
    }
    m.current_a = (m.sens != 0.0f) ? (m.sensor_v - m.zero_v) / m.sens : 0.0f;

    // Warnings (indication only).
    const float ia = fabsf(m.current_a);
    const bool rel_hi = !m.engaged && m.st != ST_TEST && (t - m.released_ms) >= (int64_t)m.cfg.auto_zero_after_ms &&
                        m.cfg.release_warn_a > 0.0f && ia > m.cfg.release_warn_a;
    if (rel_hi && !m.release_high) {
        printf("RIG,MAGNET,WARN,%.2f A flowing with the magnet RELEASED (%.0f %% duty) - the load may not let go; "
               "consider release_duty_pct = 0 in main.c\n", ia, m.duty);
    }
    m.release_high = rel_hi;
    const bool oc = m.engaged && m.cfg.over_current_warn_a > 0.0f && ia > m.cfg.over_current_warn_a;
    if (oc && !m.over_current) {
        printf("RIG,MAGNET,WARN,%.2f A engaged - above %.1f A (coil / sensor rating). Check coil temperature; "
               "lower on_duty_pct in main.c if needed\n", ia, m.cfg.over_current_warn_a);
    }
    m.over_current = oc;
}

static void update_status(int64_t t)
{
    if (!m.engaged) {
        m.status = 0;
        m.fault = false;
        return;
    }
    if (m.cfg.hold_check && m.sensor_fault) {   // engaged but the sensor reading is implausible
        if (!m.fault) printf("RIG,MAGNET,FAULT,current sensor implausible while engaged - status 2 + fault b4\n");
        m.fault = true;
        m.status = 2;
        return;
    }
    if (!m.cfg.hold_check || !m.cur_valid) {   // no measurement: report from the command
        m.status = 1;
        return;
    }
    if (fabsf(m.current_a) >= m.threshold) {
        if (m.fault) printf("RIG,MAGNET,HOLD,current %.2f A >= %.2f A - fault cleared\n", fabsf(m.current_a), m.threshold);
        m.fault = false;
        m.status = 1;
    } else if ((t - m.engaged_ms) >= (int64_t)m.cfg.hold_timeout_ms) {
        if (!m.fault) {
            printf("RIG,MAGNET,FAULT,current %.2f A below the %.2f A holding threshold %u ms after engaging - "
                   "status 2 + fault b4 (magnet NOT switched off)\n",
                   fabsf(m.current_a), m.threshold, (unsigned)m.cfg.hold_timeout_ms);
        }
        m.fault = true;
        m.status = 2;
    } else {
        m.status = 1;   // still within hold_timeout_ms of engaging
    }
}

// -----------------------------------------------------------------------------
// Console requests (executed in the magnet task)
// -----------------------------------------------------------------------------
static void run_request(const req_t *r)
{
    switch (r->type) {
    case REQ_ZERO:
        if (m.engaged || m.duty > m.cfg.release_duty_pct) {
            printf("ERR,MAGNET,ZERO,release the magnet first (zero is the sensor output at 0 A)\n");
        } else if (!m.cur_valid) {
            printf("ERR,MAGNET,ZERO,no current-sensor reading yet\n");
        } else {
            m.zero_v = m.sensor_v;
            printf("OK,MAGNET,ZERO,%.4f V = 0 A\n", m.zero_v);
        }
        return;
    case REQ_CAL: {
        const float dv = m.sensor_v - m.zero_v;
        if (!m.engaged || !m.cur_valid) {
            printf("ERR,MAGNET,CAL,engage the magnet first (MAGNET ON) and wait for a reading\n");
        } else if (fabsf(dv) < 0.02f) {
            printf("ERR,MAGNET,CAL,sensor only moved %.3f V from zero - is current flowing?\n", dv);
        } else {
            m.sens = dv / r->value;
            printf("OK,MAGNET,CAL,%.4f V at %.2f A -> %.4f V/A (zero %.4f V). Put this in main.c to keep it.\n",
                   m.sensor_v, r->value, m.sens, m.zero_v);
        }
        return;
    }
    case REQ_THRESHOLD:
        m.threshold = r->value;
        m.fault = false;
        printf("OK,MAGNET,THRESHOLD,%.2f A (until reboot; main.c holds the default)\n", m.threshold);
        return;
    case REQ_RESET:
        m.fault = false;
        m.engaged_ms = now_ms();
        printf("OK,MAGNET,RESET,fault cleared\n");
        return;
    case REQ_STOP:
        release();
        go(m.st == ST_TEST ? ST_OFF : m.st);
        printf("OK,MAGNET,STOP,released (PWM %.0f %%, MAG_LOGIC LOW) - dashboard control restored\n",
               m.cfg.release_duty_pct);
        return;
    default:
        break;
    }

    if (m.st != ST_TEST) {
        go(ST_TEST);
        printf("RIG,MAGNET,TEST,dashboard magnet commands ignored until MAGNET STOP\n");
    }
    switch (r->type) {
    case REQ_PWM:
        m.lead_pending = false;
        set_duty(r->value);
        printf("OK,MAGNET,PWM,J7.5 %.1f %% at %u Hz, MAG_LOGIC %s (unchanged)\n", m.duty, (unsigned)m.cfg.pwm_hz,
               m.logic ? "HIGH" : "LOW");
        break;
    case REQ_ON:
        engage();
        printf("OK,MAGNET,ON\n");
        break;
    case REQ_OFF:
        release();
        printf("OK,MAGNET,OFF\n");
        break;
    case REQ_LOGIC:
        set_logic(r->flag != 0);
        printf("OK,MAGNET,LOGIC,J3.6 %s\n", (r->flag != 0) == m.cfg.logic_on_high ? "HIGH" : "LOW");
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// Magnet task: 50 Hz
// -----------------------------------------------------------------------------
static void magnet_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        const int64_t t = now_ms();

        bool have_cmd, reset_req;
        uint8_t cmd_state;
        req_t reqs[REQ_SLOTS];
        int nreq = 0;
        xSemaphoreTake(m.lock, portMAX_DELAY);
        have_cmd = m.have_cmd;
        cmd_state = m.cmd_state;
        reset_req = m.reset_req;
        m.reset_req = false;
        while (m.req_count) {
            reqs[nreq++] = m.req[m.req_head];
            m.req_head = (uint8_t)((m.req_head + 1) % REQ_SLOTS);
            m.req_count--;
        }
        xSemaphoreGive(m.lock);

        update_current(t);
        for (int i = 0; i < nreq; i++) run_request(&reqs[i]);
        if (reset_req && m.fault) {
            m.fault = false;
            m.engaged_ms = t;
        }

        // Dashboard command (not in test mode). The last state is kept through a
        // lost link or soft stop: the magnet is never dropped automatically.
        if (m.st != ST_TEST && have_cmd) {
            if (cmd_state && !m.engaged) {
                engage();
                go(ST_ON);
            } else if (!cmd_state && m.engaged) {
                release();
                go(ST_OFF);
            }
        }

        if (m.lead_pending && (t - m.engaged_ms) >= (int64_t)m.cfg.logic_lead_ms) {
            m.lead_pending = false;
            set_duty(m.cfg.on_duty_pct);
        }
        update_status(t);
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
esp_err_t magnet_start(const magnet_cfg_t *cfg)
{
    memset(&m, 0, sizeof(m));
    m.cfg = *cfg;
    m.lock = xSemaphoreCreateMutexStatic(&m.lock_buf);
    m.zero_v = cfg->zero_v;
    m.sens = cfg->sens_v_per_a;
    m.threshold = cfg->hold_threshold_a;
    m.st = ST_OFF;
    m.released_ms = 0;

    // 1. MAG_LOGIC LOW first.
    if (mcp_shared_ok()) {
        const int off = cfg->logic_on_high ? 0 : 1;
        if (mcp_shared_config_output(MCP23017_PORT_A, cfg->logic_bit, off) != ESP_OK) {
            ESP_LOGE(TAG, "MAG_LOGIC (MCP GPA%d, J3.6) could not be configured", cfg->logic_bit);
        }
    } else {
        ESP_LOGE(TAG, "MCP23017 missing: MAG_LOGIC (J3.6) NOT driven");
    }

    // 2. PWM on J7.5 at the boot duty.
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
    m.pwm_ok = (err == ESP_OK);
    if (!m.pwm_ok) ESP_LOGE(TAG, "MAG_PWM on GPIO%d failed (%s)", cfg->pwm_gpio, esp_err_to_name(err));
    set_duty(cfg->boot_duty_pct);

    // 3. Current sensor through the load-cell task (shared ADS1220).
    if (cfg->sense_enabled) loadcell_set_aux(cfg->adc_channel, cfg->aux_period_ms);

    m.present = true;
    xTaskCreate(magnet_task, "magnet", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
    ESP_LOGI(TAG, "magnet ready: PWM GPIO%d (J7.5) %u Hz %s, ON %.0f %% / release %.0f %% / boot %.0f %%, MAG_LOGIC GPA%d "
                  "(J3.6) HIGH=%s; current AIN%u (J6.8) every %u ms: zero %.3f V, %.4f V/A, AVDD %.2f V; hold >= %.2f A "
                  "within %u ms",
             cfg->pwm_gpio, (unsigned)cfg->pwm_hz, m.pwm_ok ? "ok" : "FAILED", cfg->on_duty_pct, cfg->release_duty_pct,
             cfg->boot_duty_pct, cfg->logic_bit, cfg->logic_on_high ? "on" : "off", cfg->adc_channel,
             (unsigned)cfg->aux_period_ms, cfg->zero_v, cfg->sens_v_per_a, cfg->avdd_v, cfg->hold_threshold_a,
             (unsigned)cfg->hold_timeout_ms);
    magnet_print("MAGNET");
    return m.pwm_ok ? ESP_OK : err;
}

void magnet_on_move(const icd_downlink_t *cmd, int64_t now_us)
{
    (void)now_us;
    if (!m.present) return;
    xSemaphoreTake(m.lock, portMAX_DELAY);
    m.have_cmd = true;
    m.cmd_state = cmd->magnet_state ? 1 : 0;
    if (cmd->mode_flags & ICD_MODE_RESET) m.reset_req = true;
    xSemaphoreGive(m.lock);
}

static bool is_number(const char *s)
{
    if (!s || !*s) return false;
    char *end;
    strtof(s, &end);
    return *end == '\0' || *end == '%' || *end == 'A';
}

static void print_help(void)
{
    printf("RIG,MAGNET,HELP,MAGNET [STATUS] | MAGNET PWM [duty%%=50] | MAGNET ON | MAGNET OFF | MAGNET LOGIC <0|1> | "
           "MAGNET ZERO | MAGNET CAL <amps> | MAGNET THRESHOLD <amps> | MAGNET RESET | MAGNET STOP\n");
    printf("RIG,MAGNET,HELP,PWM = duty on J7.5 at %u Hz with MAG_LOGIC unchanged; ON / OFF = full engage / release; "
           "all ignore the dashboard until MAGNET STOP\n", (unsigned)m.cfg.pwm_hz);
}

void magnet_command(const char *sub, const char *a1, const char *a2)
{
    (void)a2;
    if (!m.present) {
        printf("ERR,MAGNET,magnet firmware not running (see boot log)\n");
        return;
    }
    if (!sub || !strcmp(sub, "STATUS")) {
        magnet_print("MAGNET");
        printf("OK,MAGNET\n");
        return;
    }
    if (!strcmp(sub, "HELP")) {
        print_help();
        printf("OK,MAGNET,HELP\n");
        return;
    }
    req_t r = {.type = REQ_NONE};
    if (!strcmp(sub, "PWM")) {
        r.type = REQ_PWM;
        r.value = a1 ? strtof(a1, NULL) : 50.0f;
        if ((a1 && !is_number(a1)) || r.value < 0.0f || r.value > 100.0f) {
            printf("ERR,usage: MAGNET PWM [duty 0-100 %%]\n");
            return;
        }
    } else if (!strcmp(sub, "ON")) {
        r.type = REQ_ON;
    } else if (!strcmp(sub, "OFF")) {
        r.type = REQ_OFF;
    } else if (!strcmp(sub, "LOGIC")) {
        if (!a1 || (strcmp(a1, "0") && strcmp(a1, "1"))) {
            printf("ERR,usage: MAGNET LOGIC <0|1>\n");
            return;
        }
        r.type = REQ_LOGIC;
        r.flag = !strcmp(a1, "1");
    } else if (!strcmp(sub, "ZERO")) {
        r.type = REQ_ZERO;
    } else if (!strcmp(sub, "CAL")) {
        r.type = REQ_CAL;
        r.value = (a1 && is_number(a1)) ? strtof(a1, NULL) : 0.0f;
        if (r.value <= 0.05f || r.value > 20.0f) {
            printf("ERR,usage: MAGNET CAL <amps flowing now, e.g. the supply reading, 0.1-20>\n");
            return;
        }
    } else if (!strcmp(sub, "THRESHOLD")) {
        r.type = REQ_THRESHOLD;
        r.value = (a1 && is_number(a1)) ? strtof(a1, NULL) : -1.0f;
        if (r.value < 0.0f || r.value > 20.0f) {
            printf("ERR,usage: MAGNET THRESHOLD <amps 0-20>\n");
            return;
        }
    } else if (!strcmp(sub, "RESET")) {
        r.type = REQ_RESET;
    } else if (!strcmp(sub, "STOP")) {
        r.type = REQ_STOP;
    } else {
        print_help();
        printf("ERR,unknown MAGNET command '%s'\n", sub);
        return;
    }
    xSemaphoreTake(m.lock, portMAX_DELAY);
    const bool busy = m.req_count >= REQ_SLOTS;
    if (!busy) {
        m.req[(m.req_head + m.req_count) % REQ_SLOTS] = r;
        m.req_count++;
    }
    xSemaphoreGive(m.lock);
    if (busy) printf("ERR,MAGNET,busy - %d MAGNET commands already waiting\n", REQ_SLOTS);
}

void magnet_get_status(magnet_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->present = m.present;
    if (!m.present) {
        out->state = "OFF";
        return;
    }
    out->test_mode = m.st == ST_TEST;
    out->state = ST_NAME[m.st];
    out->engaged = m.engaged;
    out->duty_pct = m.duty;
    out->logic = m.logic;
    out->current_valid = m.cur_valid;
    out->sensor_fault = m.sensor_fault;
    out->release_current_high = m.release_high;
    out->over_current = m.over_current;
    out->sensor_v = m.sensor_v;
    out->zero_v = m.zero_v;
    out->current_a = m.current_a;
    out->status = m.status;
    out->fault = m.fault;
}

void magnet_print(const char *tag)
{
    magnet_status_t s;
    magnet_get_status(&s);
    printf("RIG,%s,state=%s,engaged=%d,duty=%.1f,pwm=%uHz,logic_j3_6=%d,sensor_v=%.4f,zero_v=%.4f,current_a=%.2f,"
           "valid=%d,sensor_fault=%d,status=%u,fault=%d,threshold_a=%.2f,v_per_a=%.4f,release_i_high=%d,over_current=%d\n",
           tag, s.state, s.engaged, s.duty_pct, (unsigned)m.cfg.pwm_hz,
           s.logic == m.cfg.logic_on_high ? 1 : 0, s.sensor_v, s.zero_v, s.current_a, s.current_valid, s.sensor_fault, s.status, s.fault,
           m.threshold, m.sens, s.release_current_high, s.over_current);
}
