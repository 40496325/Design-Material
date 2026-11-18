// drive_control.c - see drive_control.h
#include "drive_control.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "drive_spwm.h"
#include "mcp_shared.h"
#include "rig_telemetry.h"

static const char *TAG = "drive";

#define TICK_MS 10
#define INPUT_DEBOUNCE_TICKS 2   // an input must read active on 2 consecutive 10 ms polls

typedef enum { ST_IDLE, ST_STARTING, ST_RUN, ST_STOPPING, ST_FAULT, ST_TEST } st_t;
static const char *const ST_NAME[] = {"IDLE", "STARTING", "RUN", "STOPPING", "FAULT", "TEST"};

typedef enum { REQ_NONE, REQ_SQUARE, REQ_SINE, REQ_EN, REQ_BRAKE, REQ_STOP, REQ_RESET } req_type_t;
typedef struct {
    req_type_t type;
    uint8_t mask;
    float value;    // duty (0..1) or sine Hz
    int flag;       // reverse / on / release
} req_t;

static struct {
    drive_cfg_t cfg;
    bool present;
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_buf;

    // Written by the radio task (under lock)
    icd_downlink_t cmd;
    int64_t cmd_us;
    bool have_cmd;
    bool reset_req;
    req_t req[4];            // console requests, FIFO
    uint8_t req_head, req_count;

    // Drive task state
    st_t st;
    int64_t st_since_ms;
    float f;                 // applied sine frequency
    int dir;                 // +1 up, -1 down, 0 none
    bool drv_en, brake_released;
    bool fast_stop;
    bool need_zero;          // after a fault reset / test: wait for hoist_cmd = 0 before starting again
    int last_dir;
    int64_t stopped_ms;
    bool waiting_reverse;
    uint8_t faults;          // latched ICD fault bits
    uint8_t cnt_fault, cnt_oc;
    int in_fault, in_oc;     // last raw input levels, -1 = not read
    bool mcp_err_reported;
} d;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

// -----------------------------------------------------------------------------
// Outputs
// -----------------------------------------------------------------------------
static void set_drv_en(bool on)
{
    if (on == d.drv_en) return;
    d.drv_en = on;
    const int level = on ? (d.cfg.drv_en_active_high ? 1 : 0) : (d.cfg.drv_en_active_high ? 0 : 1);
    if (mcp_shared_write(MCP23017_PORT_A, d.cfg.drv_en_bit, level) != ESP_OK && mcp_shared_ok()) {
        printf("RIG,WARN,DRIVE,DRV_EN write to MCP23017 failed\n");
    }
}

static void set_brake(bool release)
{
    if (release == d.brake_released) return;
    d.brake_released = release;
    if (!d.cfg.brake_enabled) return;
    const int level = release ? (d.cfg.brake_release_high ? 1 : 0) : (d.cfg.brake_release_high ? 0 : 1);
    if (mcp_shared_write(MCP23017_PORT_B, d.cfg.brake_bit, level) != ESP_OK && mcp_shared_ok()) {
        printf("RIG,WARN,DRIVE,brake write to MCP23017 failed\n");
    }
}

static void all_off_now(void)
{
    drive_spwm_idle();
    set_brake(false);
    set_drv_en(false);
    if (d.dir) d.last_dir = d.dir;
    d.f = 0.0f;
    d.dir = 0;
    d.stopped_ms = now_ms();
}

static float m_for(float f)
{
    float m = d.cfg.m_max * f / d.cfg.f_base_hz;   // V/f: amplitude proportional to frequency
    if (m < d.cfg.m_boost) m = d.cfg.m_boost;
    if (m > d.cfg.m_max) m = d.cfg.m_max;
    return m;
}

static bool reverse_for(int dir)
{
    // UP = A-B-C when up_is_abc; the other direction swaps B and C.
    return (dir > 0) ? !d.cfg.up_is_abc : d.cfg.up_is_abc;
}

static void go(st_t st)
{
    if (st == d.st) return;
    printf("RIG,DRIVE,STATE,%s->%s,f=%.1f,dir=%s,drv_en=%d,brake=%s\n", ST_NAME[d.st], ST_NAME[st], d.f,
           d.dir > 0 ? "UP" : (d.dir < 0 ? "DOWN" : "-"), d.drv_en, d.brake_released ? "RELEASED" : "CLAMPED");
    d.st = st;
    d.st_since_ms = now_ms();
}

// -----------------------------------------------------------------------------
// Inputs (DRV_FAULT J3.5, VFD overcurrent J3.8)
// -----------------------------------------------------------------------------
static void poll_inputs(void)
{
    if (!mcp_shared_ok() || (!d.cfg.fault_enabled && !d.cfg.oc_enabled)) return;
    uint8_t v;
    if (mcp_shared_read_port(MCP23017_PORT_A, &v) != ESP_OK) {
        if (!d.mcp_err_reported) printf("RIG,WARN,DRIVE,MCP23017 read failed - fault inputs not checked\n");
        d.mcp_err_reported = true;
        return;
    }
    d.mcp_err_reported = false;

    if (d.cfg.fault_enabled) {
        d.in_fault = (v >> d.cfg.fault_bit) & 1;
        d.cnt_fault = (d.in_fault == d.cfg.fault_active_level) ? (uint8_t)(d.cnt_fault + 1) : 0;
        if (d.cnt_fault >= INPUT_DEBOUNCE_TICKS && !(d.faults & ICD_FAULT_VFD)) {
            d.faults |= ICD_FAULT_VFD;
            printf("RIG,DRIVE,FAULT,DRV_FAULT (J3.5) active - drive stopped, latched until RESET\n");
        }
    }
    if (d.cfg.oc_enabled) {
        d.in_oc = (v >> d.cfg.oc_bit) & 1;
        d.cnt_oc = (d.in_oc == d.cfg.oc_active_level) ? (uint8_t)(d.cnt_oc + 1) : 0;
        if (d.cnt_oc >= INPUT_DEBOUNCE_TICKS && !(d.faults & ICD_FAULT_VFD_OVERCURRENT)) {
            d.faults |= ICD_FAULT_VFD_OVERCURRENT;
            printf("RIG,DRIVE,FAULT,VFD overcurrent (J3.8) active - drive stopped, latched until RESET\n");
        }
    }
}

static bool inputs_clear(void)
{
    return d.cnt_fault == 0 && d.cnt_oc == 0;
}

// -----------------------------------------------------------------------------
// Console test requests (executed in the drive task)
// -----------------------------------------------------------------------------
static void run_request(const req_t *r)
{
    switch (r->type) {
    case REQ_RESET:
        if (d.st != ST_FAULT && !d.faults) {
            printf("OK,DRIVE,RESET,no faults latched\n");
        } else if (!inputs_clear()) {
            printf("ERR,DRIVE,RESET refused - a fault input is still active (in_fault=%d,in_oc=%d)\n", d.in_fault,
                   d.in_oc);
        } else {
            d.faults = 0;
            d.need_zero = true;
            go(ST_IDLE);
            printf("OK,DRIVE,RESET,faults cleared - set the hoist to 0 before driving again\n");
        }
        return;
    case REQ_STOP:
        all_off_now();
        if (d.st != ST_FAULT) {
            d.need_zero = true;
            go(ST_IDLE);
        }
        printf("OK,DRIVE,STOP,outputs idle, DRV_EN off, brake clamped%s\n",
               d.st == ST_FAULT ? " (fault still latched)" : " - dashboard control restored");
        return;
    default:
        break;
    }

    // Test modes: only from rest, never over a fault.
    if (d.st == ST_FAULT) {
        printf("ERR,DRIVE,fault latched (flags=%02X) - DRIVE RESET first\n", d.faults);
        return;
    }
    if (d.st != ST_IDLE && d.st != ST_TEST) {
        printf("ERR,DRIVE,hoist is %s - bring it to rest (dashboard hoist 0) or DRIVE STOP first\n", ST_NAME[d.st]);
        return;
    }
    if (d.st == ST_IDLE) {
        go(ST_TEST);
        printf("RIG,DRIVE,TEST,dashboard hoist commands ignored until DRIVE STOP\n");
    }

    switch (r->type) {
    case REQ_SQUARE:
        drive_spwm_fixed(r->mask, r->value);
        d.dir = 0;
        d.f = 0.0f;
        printf("OK,DRIVE,SQUARE,phases=%s%s%s,pin_duty=%.1f%%,freq=%uHz (at the MCU pin; IR2104 IN sees %.1f%%)\n",
               (r->mask & DRIVE_PHASE_A) ? "A" : "", (r->mask & DRIVE_PHASE_B) ? "B" : "",
               (r->mask & DRIVE_PHASE_C) ? "C" : "", r->value * 100.0f, (unsigned)d.cfg.carrier_hz,
               (d.cfg.opto_inverts ? 1.0f - r->value : r->value) * 100.0f);
        break;
    case REQ_SINE: {
        const int dir = r->flag ? -1 : 1;
        if (drive_spwm_mode() == DRIVE_SPWM_SINE && dir != d.dir) {
            printf("ERR,DRIVE,SINE,direction change needs a stop first (EEII 1) - DRIVE STOP, then DRIVE SINE\n");
            break;
        }
        const float f = r->value;
        if (drive_spwm_start_sine(f, m_for(f), reverse_for(dir)) == ESP_OK) {
            d.dir = dir;
            d.f = drive_spwm_freq();
            printf("OK,DRIVE,SINE,f=%.1fHz,m=%.2f,dir=%s,order=%s,carrier=%uHz\n", d.f, drive_spwm_m(),
                   dir > 0 ? "UP" : "DOWN", reverse_for(dir) ? "A-C-B" : "A-B-C", (unsigned)d.cfg.carrier_hz);
        } else {
            printf("ERR,DRIVE,SINE,could not start\n");
        }
        break;
    }
    case REQ_EN:
        set_drv_en(r->flag != 0);
        printf("OK,DRIVE,EN,%d (J3.4 level %d)\n", d.drv_en,
               d.drv_en == d.cfg.drv_en_active_high ? 1 : 0);
        break;
    case REQ_BRAKE:
        set_brake(r->flag != 0);
        printf("OK,DRIVE,BRAKE,%s%s\n", d.brake_released ? "RELEASED" : "CLAMPED",
               d.cfg.brake_enabled ? "" : " (brake output disabled in main.c)");
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// Drive task: 100 Hz
// -----------------------------------------------------------------------------
static void drive_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();
    const float dt = TICK_MS / 1000.0f;

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        const int64_t t = now_ms();

        poll_inputs();

        // Snapshot the latest command / requests.
        icd_downlink_t cmd;
        int64_t cmd_us;
        bool have_cmd, reset_req;
        req_t reqs[4];
        int nreq = 0;
        xSemaphoreTake(d.lock, portMAX_DELAY);
        cmd = d.cmd;
        cmd_us = d.cmd_us;
        have_cmd = d.have_cmd;
        reset_req = d.reset_req;
        d.reset_req = false;
        while (d.req_count) {                 // every queued console request, in order
            reqs[nreq++] = d.req[d.req_head];
            d.req_head = (uint8_t)((d.req_head + 1) % 4);
            d.req_count--;
        }
        xSemaphoreGive(d.lock);

        for (int i = 0; i < nreq; i++) {
            poll_inputs();                    // a fault must still win over a queued test command
            if (d.faults && d.st != ST_FAULT) {
                all_off_now();
                go(ST_FAULT);
            }
            run_request(&reqs[i]);
        }

        // Faults stop everything immediately, in every state including TEST.
        if (d.faults && d.st != ST_FAULT) {
            all_off_now();
            go(ST_FAULT);
        }

        // Dashboard command -> target direction and frequency.
        const bool fresh = have_cmd && (esp_timer_get_time() - cmd_us) < (int64_t)d.cfg.link_timeout_ms * 1000;
        const bool soft_stop = (cmd.mode_flags & ICD_MODE_SOFT_STOP) != 0;
        const bool hold_brake = (cmd.mode_flags & ICD_MODE_HOLD_BRAKE) != 0;
        int pct = (fresh && !soft_stop && !hold_brake) ? cmd.hoist_cmd : 0;
        if (pct > 0 && rig_tlm_overloaded()) pct = 0;                  // ICD IF13.0 overload inhibit: no lifting
        if (abs(pct) < d.cfg.cmd_deadband_pct) pct = 0;
        const int tgt_dir = pct > 0 ? 1 : (pct < 0 ? -1 : 0);
        float tgt_f = (float)abs(pct) / 100.0f * d.cfg.f_max_hz;
        if (tgt_dir && tgt_f < d.cfg.f_start_hz) tgt_f = d.cfg.f_start_hz;
        const bool fast = !fresh || soft_stop;
        if (tgt_dir == 0) d.need_zero = false;

        switch (d.st) {
        case ST_FAULT:
            if (reset_req) {
                if (inputs_clear()) {
                    d.faults = 0;
                    d.need_zero = true;
                    go(ST_IDLE);
                    printf("RIG,DRIVE,RESET,faults cleared from the dashboard - set the hoist to 0 before driving again\n");
                } else {
                    printf("RIG,DRIVE,RESET refused - a fault input is still active\n");
                }
            }
            break;

        case ST_TEST:
            break;   // outputs are set by the console requests only

        case ST_IDLE:
            d.waiting_reverse = false;
            if (tgt_dir && !d.need_zero) {
                // EEII 1: stop first, then swap. A start in the opposite direction
                // waits until the motor has been at rest for reverse_dwell_ms.
                if (tgt_dir == -d.last_dir && (t - d.stopped_ms) < (int64_t)d.cfg.reverse_dwell_ms) {
                    d.waiting_reverse = true;
                    break;
                }
                d.dir = tgt_dir;
                d.f = d.cfg.f_start_hz;
                set_drv_en(true);
                drive_spwm_start_sine(d.f, m_for(d.f), reverse_for(d.dir));
                go(ST_STARTING);
            }
            break;

        case ST_STARTING:
            if (tgt_dir != d.dir || fast) {
                d.fast_stop = fast;
                go(ST_STOPPING);
            } else if (t - d.st_since_ms >= (int64_t)d.cfg.brake_delay_ms) {
                set_brake(true);   // motor is energised: now let the brake go
                go(ST_RUN);
            }
            break;

        case ST_RUN:
            if (tgt_dir != d.dir || fast) {
                d.fast_stop = fast;
                go(ST_STOPPING);
                break;
            }
            if (d.f < tgt_f) d.f = fminf(tgt_f, d.f + d.cfg.ramp_hz_s * dt);
            else if (d.f > tgt_f) d.f = fmaxf(tgt_f, d.f - d.cfg.ramp_hz_s * dt);
            drive_spwm_set_sine(d.f, m_for(d.f));
            break;

        case ST_STOPPING:
            if (d.brake_released) {
                // Same direction asked for again (not a lost link / soft stop): carry on.
                if (tgt_dir == d.dir && !fast && !d.fast_stop) {
                    go(ST_RUN);
                    break;
                }
                const float rate = (d.fast_stop || fast) ? d.cfg.stop_ramp_hz_s : d.cfg.ramp_hz_s;
                d.f = fmaxf(d.cfg.f_start_hz, d.f - rate * dt);
                drive_spwm_set_sine(d.f, m_for(d.f));
                if (d.f <= d.cfg.f_start_hz) {
                    set_brake(false);          // clamp at the lowest speed, motor still holding
                    d.st_since_ms = t;
                }
            } else if (t - d.st_since_ms >= (int64_t)d.cfg.brake_delay_ms) {
                all_off_now();                 // brake has closed: switching off, DRV_EN off
                d.fast_stop = false;
                go(ST_IDLE);
            }
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
esp_err_t drive_start(const drive_cfg_t *cfg)
{
    memset(&d, 0, sizeof(d));
    d.cfg = *cfg;
    d.lock = xSemaphoreCreateMutexStatic(&d.lock_buf);
    d.in_fault = d.in_oc = -1;
    d.st = ST_IDLE;
    d.stopped_ms = -100000;

    // 1. DRV_EN off and brake clamped FIRST, before any switching can happen.
    if (mcp_shared_ok()) {
        const int en_off = cfg->drv_en_active_high ? 0 : 1;
        if (mcp_shared_config_output(MCP23017_PORT_A, cfg->drv_en_bit, en_off) != ESP_OK) {
            ESP_LOGE(TAG, "DRV_EN (MCP GPA%d) could not be configured", cfg->drv_en_bit);
        }
        if (cfg->brake_enabled) {
            const int clamp = cfg->brake_release_high ? 0 : 1;
            mcp_shared_config_output(MCP23017_PORT_B, cfg->brake_bit, clamp);
        }
        if (cfg->fault_enabled) mcp_shared_config_input(MCP23017_PORT_A, cfg->fault_bit, cfg->fault_pullup);
        if (cfg->oc_enabled) mcp_shared_config_input(MCP23017_PORT_A, cfg->oc_bit, cfg->oc_pullup);
    } else {
        ESP_LOGE(TAG, "MCP23017 missing: DRV_EN (J3.4), brake (J4.8) and the fault inputs are NOT driven/read");
    }

    // 2. SPWM pins: created straight into the idle level.
    drive_spwm_cfg_t scfg = {
        .gpio_a = cfg->spwm_gpio_a,
        .gpio_b = cfg->spwm_gpio_b,
        .gpio_c = cfg->spwm_gpio_c,
        .carrier_hz = cfg->carrier_hz,
        .opto_inverts = cfg->opto_inverts,
        .idle_in_level = cfg->idle_in_level,
        .duty_min = cfg->duty_min,
        .duty_max = cfg->duty_max,
        .max_drive_strength = true,
    };
    esp_err_t err = drive_spwm_init(&scfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPWM init failed (%s) - drive NOT running", esp_err_to_name(err));
        return err;
    }

    d.present = true;
    xTaskCreate(drive_task, "drive", 4096, NULL, tskIDLE_PRIORITY + 4, NULL);
    ESP_LOGI(TAG, "hoist drive ready: f %.0f-%.0f Hz, V/f m=%.2f@%.0fHz (boost %.2f), ramp %.0f Hz/s "
                  "(stop %.0f Hz/s), brake delay %u ms, reverse dwell %u ms, UP=%s",
             cfg->f_start_hz, cfg->f_max_hz, cfg->m_max, cfg->f_base_hz, cfg->m_boost, cfg->ramp_hz_s,
             cfg->stop_ramp_hz_s, (unsigned)cfg->brake_delay_ms, (unsigned)cfg->reverse_dwell_ms,
             cfg->up_is_abc ? "A-B-C" : "A-C-B");
    drive_print("DRIVE");
    return ESP_OK;
}

void drive_on_move(const icd_downlink_t *cmd, int64_t now_us)
{
    if (!d.present) return;
    xSemaphoreTake(d.lock, portMAX_DELAY);
    d.cmd = *cmd;
    d.cmd_us = now_us;
    d.have_cmd = true;
    if (cmd->mode_flags & ICD_MODE_RESET) d.reset_req = true;
    xSemaphoreGive(d.lock);
}

static uint8_t parse_phases(const char *s)
{
    if (!s || !strcmp(s, "ALL")) return DRIVE_PHASE_ALL;
    uint8_t m = 0;
    for (const char *p = s; *p; p++) {
        if (*p == 'A') m |= DRIVE_PHASE_A;
        else if (*p == 'B') m |= DRIVE_PHASE_B;
        else if (*p == 'C') m |= DRIVE_PHASE_C;
        else return 0;
    }
    return m;
}

static bool is_number(const char *s)
{
    if (!s || !*s) return false;
    char *end;
    strtof(s, &end);
    return *end == '\0' || *end == '%';
}

static void print_drive_help(void)
{
    printf("RIG,DRIVE,HELP,DRIVE [STATUS] | DRIVE SQUARE [A|B|C|AB..|ALL] [duty%%=50] | DRIVE SINE <Hz> [UP|DOWN] | "
           "DRIVE EN <0|1> | DRIVE BRAKE <CLAMP|RELEASE> | DRIVE STOP | DRIVE RESET\n");
    printf("RIG,DRIVE,HELP,SQUARE = fixed-duty %u Hz square at the MCU pin (the signal-generator test); "
           "SINE = 3-phase SPWM at a fixed frequency; both ignore the dashboard until DRIVE STOP\n",
           (unsigned)d.cfg.carrier_hz);
}

void drive_command(const char *sub, const char *a1, const char *a2)
{
    if (!d.present) {
        printf("ERR,DRIVE,drive firmware not running (see boot log)\n");
        return;
    }
    if (!sub || !strcmp(sub, "STATUS")) {
        drive_print("DRIVE");
        printf("OK,DRIVE\n");
        return;
    }
    if (!strcmp(sub, "HELP")) {
        print_drive_help();
        printf("OK,DRIVE,HELP\n");
        return;
    }

    req_t r = {.type = REQ_NONE};
    if (!strcmp(sub, "SQUARE")) {
        // DRIVE SQUARE            -> all phases, 50 %
        // DRIVE SQUARE A          -> phase A, 50 %
        // DRIVE SQUARE 30         -> all phases, 30 %
        // DRIVE SQUARE B 30       -> phase B, 30 %
        const char *ph = a1, *du = a2;
        if (is_number(a1)) {
            ph = NULL;
            du = a1;
        }
        r.type = REQ_SQUARE;
        r.mask = parse_phases(ph);
        r.value = du ? strtof(du, NULL) / 100.0f : 0.5f;
        if (!r.mask || r.value < 0.01f || r.value > 0.99f) {
            printf("ERR,usage: DRIVE SQUARE [A|B|C|AB|ALL] [duty 1-99 %%]\n");
            return;
        }
    } else if (!strcmp(sub, "SINE")) {
        r.type = REQ_SINE;
        r.value = a1 ? strtof(a1, NULL) : 0.0f;
        r.flag = (a2 && !strcmp(a2, "DOWN")) ? 1 : 0;
        if (r.value < 0.5f || r.value > 100.0f || (a2 && strcmp(a2, "UP") && strcmp(a2, "DOWN"))) {
            printf("ERR,usage: DRIVE SINE <0.5-100 Hz> [UP|DOWN]\n");
            return;
        }
    } else if (!strcmp(sub, "EN")) {
        if (!a1 || (strcmp(a1, "0") && strcmp(a1, "1") && strcmp(a1, "ON") && strcmp(a1, "OFF"))) {
            printf("ERR,usage: DRIVE EN <0|1>\n");
            return;
        }
        r.type = REQ_EN;
        r.flag = (!strcmp(a1, "1") || !strcmp(a1, "ON")) ? 1 : 0;
    } else if (!strcmp(sub, "BRAKE")) {
        if (!a1 || (strcmp(a1, "CLAMP") && strcmp(a1, "RELEASE") && strcmp(a1, "0") && strcmp(a1, "1"))) {
            printf("ERR,usage: DRIVE BRAKE <CLAMP|RELEASE>\n");
            return;
        }
        r.type = REQ_BRAKE;
        r.flag = (!strcmp(a1, "RELEASE") || !strcmp(a1, "1")) ? 1 : 0;
    } else if (!strcmp(sub, "STOP")) {
        r.type = REQ_STOP;
    } else if (!strcmp(sub, "RESET")) {
        r.type = REQ_RESET;
    } else {
        print_drive_help();
        printf("ERR,unknown DRIVE command '%s'\n", sub);
        return;
    }

    xSemaphoreTake(d.lock, portMAX_DELAY);
    const bool busy = d.req_count >= 4;
    if (!busy) {
        d.req[(d.req_head + d.req_count) % 4] = r;
        d.req_count++;
    }
    xSemaphoreGive(d.lock);
    if (busy) printf("ERR,DRIVE,busy - 4 DRIVE commands already waiting\n");
    // The drive task executes it within 10 ms and prints OK/ERR.
}

void drive_get_status(drive_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->present = d.present;
    if (!d.present) {
        out->state = "OFF";
        return;
    }
    out->test_mode = d.st == ST_TEST;
    out->state = (d.st == ST_IDLE && d.waiting_reverse) ? "REVERSE_DWELL" : ST_NAME[d.st];
    out->drv_en = d.drv_en;
    out->brake_released = d.brake_released;
    out->freq_hz = drive_spwm_freq();
    out->m = drive_spwm_m();
    out->dir = (drive_spwm_mode() == DRIVE_SPWM_SINE) ? d.dir : 0;
    out->speed_pct = (float)out->dir * out->freq_hz / d.cfg.f_max_hz * 100.0f;
    out->hoist_vel_mm_s = (float)out->dir * out->freq_hz / d.cfg.f_max_hz * d.cfg.hoist_mm_s_at_fmax;
    out->fault_flags = d.faults;
}

void drive_print(const char *tag)
{
    drive_status_t s;
    drive_get_status(&s);
    const drive_spwm_mode_t mode = drive_spwm_mode();
    const uint8_t mask = drive_spwm_phase_mask();
    printf("RIG,%s,state=%s,spwm=%s,carrier=%uHz,f=%.1f,m=%.2f,dir=%s,phases=%s%s%s,pin_duty=%.1f,drv_en=%d,"
           "brake=%s,faults=%02X,in_fault_j3_5=%d,in_oc_j3_8=%d,isr=%u\n",
           tag, s.state, mode == DRIVE_SPWM_SINE ? "SINE" : (mode == DRIVE_SPWM_FIXED ? "SQUARE" : "IDLE"),
           (unsigned)d.cfg.carrier_hz, s.freq_hz, s.m, s.dir > 0 ? "UP" : (s.dir < 0 ? "DOWN" : "-"),
           (mask & DRIVE_PHASE_A) ? "A" : "", (mask & DRIVE_PHASE_B) ? "B" : "", (mask & DRIVE_PHASE_C) ? "C" : "",
           mode == DRIVE_SPWM_FIXED ? drive_spwm_fixed_duty() * 100.0f : 0.0f, s.drv_en,
           s.brake_released ? "RELEASED" : "CLAMPED", s.fault_flags, d.in_fault, d.in_oc,
           (unsigned)drive_spwm_isr_count());
}
