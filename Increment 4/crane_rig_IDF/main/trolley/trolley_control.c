// trolley_control.c - see trolley_control.h
#include "trolley_control.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "trolley_io.h"

static const char *TAG = "trolley";

#define TICK_MS 10
#define VEL_WINDOW_TICKS 10   // speed measured over 100 ms
#define REQ_SLOTS 4

typedef enum { ST_IDLE, ST_RUN, ST_STOPPING, ST_TEST } st_t;
static const char *const ST_NAME[] = {"IDLE", "RUN", "STOPPING", "TEST"};

typedef enum { REQ_NONE, REQ_PWM, REQ_RUN, REQ_IN, REQ_STOP, REQ_ZERO, REQ_LIMITS, REQ_RESET } req_type_t;
typedef struct {
    req_type_t type;
    float value;   // duty 0..100
    int flag;      // direction / IN state / on-off
} req_t;

static struct {
    trolley_cfg_t cfg;
    bool present;
    float mm_per_count;
    SemaphoreHandle_t lock;
    StaticSemaphore_t lock_buf;

    // Written by the radio task (under lock)
    icd_downlink_t cmd;
    int64_t cmd_us;
    bool have_cmd;
    bool reset_req, zero_req;
    req_t req[REQ_SLOTS];
    uint8_t req_head, req_count;

    // Trolley task
    st_t st;
    int dir;                 // applied direction
    float duty;              // applied duty, %
    int last_dir;            // direction of the last motion (for counts while coasting)
    int64_t stopped_ms;
    bool waiting_reverse;
    bool fast_stop;
    float pos_mm;
    float vel_mm_s;
    int32_t win_counts;
    int win_ticks;
    bool zeroed;
    bool limits_on;
    bool rail_limit;
    bool encoder_fault;
    int64_t no_count_since_ms;
    int64_t last_count_ms;   // last time the encoder counted (standstill detection)
    int test_dir;            // direction the console RUN asked for
    float test_duty;
} d;

static int64_t now_ms(void) { return esp_timer_get_time() / 1000; }

static void go(st_t st)
{
    if (st == d.st) return;
    printf("RIG,TROLLEY,STATE,%s->%s,duty=%.1f,dir=%s,in=%s,pos=%.0f\n", ST_NAME[d.st], ST_NAME[st], d.duty,
           d.dir > 0 ? "FWD" : (d.dir < 0 ? "REV" : "-"), trolley_io_in_name(trolley_io_in()), d.pos_mm);
    d.st = st;
}

static void apply(void)
{
    trolley_io_set_duty(d.duty / 100.0f);
}

static void coast_now(void)
{
    d.duty = 0.0f;
    apply();
    trolley_io_set_in(TROLLEY_IN_COAST);
    if (d.dir) d.last_dir = d.dir;
    d.dir = 0;
    d.stopped_ms = now_ms();
}

static float slew_to(float from, float to, float rate_pct_s)
{
    const float step = rate_pct_s * (TICK_MS / 1000.0f);
    if (from < to) return fminf(to, from + step);
    return fmaxf(to, from - step);
}

// Would moving in direction dir run past an armed rail limit?
static bool limit_blocks(int dir)
{
    if (!d.cfg.soft_limits || !d.limits_on || !d.zeroed) return false;
    if (dir > 0 && d.pos_mm >= d.cfg.rail_length_mm - d.cfg.limit_margin_mm) return true;
    if (dir < 0 && d.pos_mm <= d.cfg.limit_margin_mm) return true;
    return false;
}

// Distance to the limit the trolley is heading for (armed limits only); large if none.
static float dist_to_limit(int dir)
{
    if (!d.cfg.soft_limits || !d.limits_on || !d.zeroed) return 1e9f;
    if (dir > 0) return (d.cfg.rail_length_mm - d.cfg.limit_margin_mm) - d.pos_mm;
    if (dir < 0) return d.pos_mm - d.cfg.limit_margin_mm;
    return 1e9f;
}

// -----------------------------------------------------------------------------
// Encoder: position, speed, encoder check
// -----------------------------------------------------------------------------
static void update_encoder(int64_t t)
{
    const int32_t n = trolley_io_encoder_delta();
    if (n > 0) d.last_count_ms = t;
    const int sign = d.dir ? d.dir : d.last_dir;    // a single channel has no direction of its own
    d.pos_mm += (float)(n * sign) * d.mm_per_count;
    d.win_counts += n * sign;
    if (++d.win_ticks >= VEL_WINDOW_TICKS) {
        d.vel_mm_s = (float)d.win_counts * d.mm_per_count / (VEL_WINDOW_TICKS * TICK_MS / 1000.0f);
        d.win_counts = 0;
        d.win_ticks = 0;
    }

    if (!d.cfg.enc_check || !trolley_io_encoder_ready()) return;
    const bool driving = d.dir != 0 && d.duty >= (float)d.cfg.enc_check_min_duty_pct;
    if (n > 0 || !driving) {
        d.no_count_since_ms = t;
        if (n > 0 && d.encoder_fault) {
            d.encoder_fault = false;
            printf("RIG,TROLLEY,ENCODER,counts seen again - encoder fault cleared\n");
        }
    } else if (!d.encoder_fault && (t - d.no_count_since_ms) >= (int64_t)d.cfg.enc_check_ms) {
        d.encoder_fault = true;
        printf("RIG,TROLLEY,ENCODER,no counts on J7.12 for %u ms at %.0f %% duty - fault b5 (motion not stopped)\n",
               (unsigned)d.cfg.enc_check_ms, d.duty);
    }
}

static void do_zero(const char *who)
{
    d.pos_mm = 0.0f;
    d.zeroed = true;
    d.rail_limit = false;
    printf("RIG,TROLLEY,ZERO,%s: position 0 mm, rail limits %s (0..%.0f mm, margin %.0f mm)\n", who,
           (d.cfg.soft_limits && d.limits_on) ? "ARMED" : "off", d.cfg.rail_length_mm, d.cfg.limit_margin_mm);
}

// -----------------------------------------------------------------------------
// Console requests (executed in the trolley task)
// -----------------------------------------------------------------------------
static void run_request(const req_t *r)
{
    switch (r->type) {
    case REQ_ZERO:
        do_zero("console");
        printf("OK,TROLLEY,ZERO\n");
        return;
    case REQ_LIMITS:
        d.limits_on = r->flag != 0;
        printf("OK,TROLLEY,LIMITS,%s%s\n", d.limits_on ? "ON" : "OFF",
               d.limits_on && !d.zeroed ? " (armed once the position is zeroed)" : "");
        return;
    case REQ_RESET:
        d.encoder_fault = false;
        d.rail_limit = false;
        d.no_count_since_ms = now_ms();
        printf("OK,TROLLEY,RESET,encoder / rail-limit indications cleared\n");
        return;
    case REQ_STOP:
        coast_now();
        d.test_dir = 0;
        go(ST_IDLE);
        printf("OK,TROLLEY,STOP,ENA 0 %%, IN1-IN4 LOW (coast) - dashboard control restored\n");
        return;
    default:
        break;
    }

    if (d.st == ST_RUN || d.st == ST_STOPPING) {
        printf("ERR,TROLLEY,trolley is %s - set the dashboard trolley to 0 or TROLLEY STOP first\n", ST_NAME[d.st]);
        return;
    }
    if (d.st == ST_IDLE) {
        go(ST_TEST);
        printf("RIG,TROLLEY,TEST,dashboard trolley commands ignored until TROLLEY STOP\n");
    }

    switch (r->type) {
    case REQ_PWM:
        d.duty = r->value;
        d.test_duty = r->value;
        apply();
        printf("OK,TROLLEY,PWM,J7.9 ENA %.1f %% at %u Hz, IN=%s (motor %s)\n", d.duty, (unsigned)trolley_io_pwm_hz(),
               trolley_io_in_name(trolley_io_in()),
               (trolley_io_in() == TROLLEY_IN_FWD || trolley_io_in() == TROLLEY_IN_REV) ? "DRIVEN" : "not driven");
        break;
    case REQ_RUN: {
        const int dir = r->flag;
        if (d.test_dir && dir != d.test_dir && d.duty > 0.0f) {
            printf("ERR,TROLLEY,RUN,direction change needs a stop first - TROLLEY STOP, then TROLLEY RUN\n");
            break;
        }
        if (limit_blocks(dir)) {
            printf("ERR,TROLLEY,RUN,rail limit: pos %.0f mm - only the other direction is allowed\n", d.pos_mm);
            break;
        }
        trolley_io_set_in(dir > 0 ? TROLLEY_IN_FWD : TROLLEY_IN_REV);
        d.dir = dir;
        d.last_dir = dir;
        d.test_dir = dir;
        d.duty = r->value;
        d.test_duty = r->value;
        apply();
        printf("OK,TROLLEY,RUN,%s at %.1f %% (IN1=%d IN2=%d), limits %s\n", dir > 0 ? "FWD" : "REV", d.duty,
               dir > 0, dir < 0, (d.cfg.soft_limits && d.limits_on && d.zeroed) ? "armed" : "not armed");
        break;
    }
    case REQ_IN:
        trolley_io_set_in((trolley_in_t)r->flag);
        if (d.dir) d.last_dir = d.dir;   // counts while braking / coasting keep the last direction
        d.dir = (r->flag == TROLLEY_IN_FWD) ? 1 : (r->flag == TROLLEY_IN_REV ? -1 : 0);
        if (d.dir) d.last_dir = d.dir;
        d.test_dir = d.dir;
        printf("OK,TROLLEY,IN,%s (J4.4 IN1=%d, J4.5 IN2=%d%s), ENA %.1f %%\n", trolley_io_in_name(trolley_io_in()),
               r->flag == TROLLEY_IN_FWD || r->flag == TROLLEY_IN_BRAKE, r->flag == TROLLEY_IN_REV || r->flag == TROLLEY_IN_BRAKE,
               d.cfg.mirror_in34 ? ", J4.6/J4.7 copied" : "", d.duty);
        break;
    default:
        break;
    }
}

// -----------------------------------------------------------------------------
// Trolley task: 100 Hz
// -----------------------------------------------------------------------------
static void trolley_task(void *arg)
{
    (void)arg;
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(TICK_MS));
        const int64_t t = now_ms();

        icd_downlink_t cmd;
        int64_t cmd_us;
        bool have_cmd, reset_req, zero_req;
        req_t reqs[REQ_SLOTS];
        int nreq = 0;
        xSemaphoreTake(d.lock, portMAX_DELAY);
        cmd = d.cmd;
        cmd_us = d.cmd_us;
        have_cmd = d.have_cmd;
        reset_req = d.reset_req;
        zero_req = d.zero_req;
        d.reset_req = d.zero_req = false;
        while (d.req_count) {
            reqs[nreq++] = d.req[d.req_head];
            d.req_head = (uint8_t)((d.req_head + 1) % REQ_SLOTS);
            d.req_count--;
        }
        xSemaphoreGive(d.lock);

        update_encoder(t);
        for (int i = 0; i < nreq; i++) run_request(&reqs[i]);
        if (zero_req) do_zero("dashboard Zero Position");
        if (reset_req) {
            d.encoder_fault = false;
            d.no_count_since_ms = t;
        }

        // ---- Console test mode: only the rail limit acts on its own ----
        if (d.st == ST_TEST) {
            if (d.dir && d.duty > 0.0f && limit_blocks(d.dir)) {
                coast_now();
                d.rail_limit = true;
                printf("RIG,TROLLEY,LIMIT,test run stopped at %.0f mm (rail limit)\n", d.pos_mm);
            }
            continue;
        }

        // ---- Dashboard command -> target ----
        const bool fresh = have_cmd && (esp_timer_get_time() - cmd_us) < (int64_t)d.cfg.link_timeout_ms * 1000;
        const bool soft_stop = (cmd.mode_flags & ICD_MODE_SOFT_STOP) != 0;
        int pct = (fresh && !soft_stop) ? cmd.trolley_cmd : 0;
        if (abs(pct) < d.cfg.cmd_deadband_pct) pct = 0;
        int tgt_dir = pct > 0 ? 1 : (pct < 0 ? -1 : 0);
        float tgt_duty = (float)abs(pct);
        if (tgt_dir && tgt_duty < (float)d.cfg.min_run_duty_pct) tgt_duty = (float)d.cfg.min_run_duty_pct;
        if (tgt_duty > (float)d.cfg.max_duty_pct) tgt_duty = (float)d.cfg.max_duty_pct;

        bool limited = false;
        if (tgt_dir && limit_blocks(tgt_dir)) {
            limited = true;
            tgt_dir = 0;
            tgt_duty = 0.0f;
        } else if (tgt_dir && dist_to_limit(tgt_dir) < d.cfg.slow_zone_mm &&
                   tgt_duty > (float)d.cfg.slow_zone_duty_pct) {
            tgt_duty = (float)d.cfg.slow_zone_duty_pct;   // slow-down zone near an armed limit
        }
        d.rail_limit = limited;
        const bool fast = !fresh || soft_stop || limited;

        switch (d.st) {
        case ST_IDLE:
            d.waiting_reverse = false;
            if (tgt_dir) {
                // Stop first, then the other way: coast reverse_dwell_ms before reversing.
                if (tgt_dir == -d.last_dir) {
                    const int64_t since_stop = t - d.stopped_ms;
                    const bool still = (t - d.last_count_ms) >= (int64_t)d.cfg.standstill_ms;
                    if (since_stop < (int64_t)d.cfg.reverse_dwell_ms ||
                        (!still && since_stop < (int64_t)d.cfg.reverse_max_wait_ms)) {
                        d.waiting_reverse = true;
                        break;
                    }
                }
                trolley_io_set_in(tgt_dir > 0 ? TROLLEY_IN_FWD : TROLLEY_IN_REV);
                d.dir = tgt_dir;
                d.last_dir = tgt_dir;
                d.duty = 0.0f;
                d.no_count_since_ms = t;
                go(ST_RUN);
            }
            break;

        case ST_RUN:
            if (tgt_dir != d.dir || fast) {
                d.fast_stop = fast;
                go(ST_STOPPING);
                break;
            }
            d.duty = slew_to(d.duty, tgt_duty, d.cfg.slew_pct_s);
            apply();
            break;

        case ST_STOPPING:
            if (tgt_dir == d.dir && !fast && !d.fast_stop) {   // same direction asked for again
                go(ST_RUN);
                break;
            }
            d.duty = slew_to(d.duty, 0.0f, (d.fast_stop || fast) ? d.cfg.stop_slew_pct_s : d.cfg.slew_pct_s);
            apply();
            if (d.duty <= 0.0f) {
                coast_now();
                d.fast_stop = false;
                go(ST_IDLE);
            }
            break;

        default:
            break;
        }
    }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
esp_err_t trolley_start(const trolley_cfg_t *cfg)
{
    memset(&d, 0, sizeof(d));
    d.cfg = *cfg;
    d.lock = xSemaphoreCreateMutexStatic(&d.lock_buf);
    d.st = ST_IDLE;
    d.limits_on = cfg->soft_limits;
    d.stopped_ms = -100000;
    d.mm_per_count = (float)M_PI * cfg->wheel_diameter_mm / cfg->counts_per_wheel_rev;

    trolley_io_cfg_t io = {
        .pwm_gpio = cfg->pwm_gpio,
        .pwm_hz = cfg->pwm_hz,
        .in1_bit = cfg->in1_bit,
        .in2_bit = cfg->in2_bit,
        .mirror_in34 = cfg->mirror_in34,
        .in3_bit = cfg->in3_bit,
        .in4_bit = cfg->in4_bit,
        .enc_gpio = cfg->enc_gpio,
        .enc_pullup = cfg->enc_pullup,
        .enc_glitch_ns = cfg->enc_glitch_ns,
    };
    esp_err_t err = trolley_io_init(&io);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "trolley I/O incomplete (%s) - running with what initialised", esp_err_to_name(err));
    }

    d.present = true;
    xTaskCreate(trolley_task, "trolley", 4096, NULL, tskIDLE_PRIORITY + 4, NULL);
    ESP_LOGI(TAG, "trolley ready: wheel %.0f mm, %.0f counts/turn -> %.4f mm/count, rail %.0f mm (limits %s, margin "
                  "%.0f mm, slow zone %.0f mm at <= %d %%, armed after ZERO), slew %.0f %%/s (stop %.0f %%/s), "
                  "reverse after >= %u ms and %u ms standstill",
             cfg->wheel_diameter_mm, cfg->counts_per_wheel_rev, d.mm_per_count, cfg->rail_length_mm,
             cfg->soft_limits ? "on" : "off", cfg->limit_margin_mm, cfg->slow_zone_mm, cfg->slow_zone_duty_pct,
             cfg->slew_pct_s, cfg->stop_slew_pct_s, (unsigned)cfg->reverse_dwell_ms, (unsigned)cfg->standstill_ms);
    trolley_print("TROLLEY");
    return ESP_OK;
}

void trolley_on_move(const icd_downlink_t *cmd, int64_t now_us)
{
    if (!d.present) return;
    xSemaphoreTake(d.lock, portMAX_DELAY);
    d.cmd = *cmd;
    d.cmd_us = now_us;
    d.have_cmd = true;
    if (cmd->mode_flags & ICD_MODE_RESET) d.reset_req = true;
    if (cmd->mode_flags & ICD_MODE_ZERO_POS) d.zero_req = true;
    xSemaphoreGive(d.lock);
}

static bool is_number(const char *s)
{
    if (!s || !*s) return false;
    char *end;
    strtof(s, &end);
    return *end == '\0' || *end == '%';
}

static void print_help(void)
{
    printf("RIG,TROLLEY,HELP,TROLLEY [STATUS] | TROLLEY PWM [duty%%=50] | TROLLEY RUN <duty%%> <FWD|REV> | "
           "TROLLEY IN <FWD|REV|COAST|BRAKE> | TROLLEY STOP | TROLLEY ZERO | TROLLEY LIMITS <0|1> | TROLLEY RESET\n");
    printf("RIG,TROLLEY,HELP,PWM = ENA duty on J7.9 with IN unchanged (LOW/LOW = motor not driven); RUN = motor on; "
           "both ignore the dashboard until TROLLEY STOP\n");
}

void trolley_command(const char *sub, const char *a1, const char *a2)
{
    if (!d.present) {
        printf("ERR,TROLLEY,trolley firmware not running (see boot log)\n");
        return;
    }
    if (!sub || !strcmp(sub, "STATUS")) {
        trolley_print("TROLLEY");
        printf("OK,TROLLEY\n");
        return;
    }
    if (!strcmp(sub, "HELP")) {
        print_help();
        printf("OK,TROLLEY,HELP\n");
        return;
    }

    req_t r = {.type = REQ_NONE};
    if (!strcmp(sub, "PWM")) {
        r.type = REQ_PWM;
        r.value = a1 ? strtof(a1, NULL) : 50.0f;
        if ((a1 && !is_number(a1)) || r.value < 0.0f || r.value > 100.0f) {
            printf("ERR,usage: TROLLEY PWM [duty 0-100 %%]\n");
            return;
        }
    } else if (!strcmp(sub, "RUN")) {
        // TROLLEY RUN 30 FWD  (also accepts TROLLEY RUN FWD 30)
        const char *du = a1, *dr = a2;
        if (a1 && !is_number(a1)) {
            du = a2;
            dr = a1;
        }
        r.type = REQ_RUN;
        r.value = du ? strtof(du, NULL) : -1.0f;
        r.flag = (dr && !strcmp(dr, "FWD")) ? 1 : ((dr && !strcmp(dr, "REV")) ? -1 : 0);
        if (r.value < 1.0f || r.value > 100.0f || !r.flag) {
            printf("ERR,usage: TROLLEY RUN <duty 1-100 %%> <FWD|REV>\n");
            return;
        }
    } else if (!strcmp(sub, "IN")) {
        r.type = REQ_IN;
        if (a1 && !strcmp(a1, "FWD")) r.flag = TROLLEY_IN_FWD;
        else if (a1 && !strcmp(a1, "REV")) r.flag = TROLLEY_IN_REV;
        else if (a1 && !strcmp(a1, "COAST")) r.flag = TROLLEY_IN_COAST;
        else if (a1 && !strcmp(a1, "BRAKE")) r.flag = TROLLEY_IN_BRAKE;
        else {
            printf("ERR,usage: TROLLEY IN <FWD|REV|COAST|BRAKE>\n");
            return;
        }
    } else if (!strcmp(sub, "STOP")) {
        r.type = REQ_STOP;
    } else if (!strcmp(sub, "ZERO")) {
        r.type = REQ_ZERO;
    } else if (!strcmp(sub, "LIMITS")) {
        if (!a1 || (strcmp(a1, "0") && strcmp(a1, "1") && strcmp(a1, "ON") && strcmp(a1, "OFF"))) {
            printf("ERR,usage: TROLLEY LIMITS <0|1>\n");
            return;
        }
        r.type = REQ_LIMITS;
        r.flag = (!strcmp(a1, "1") || !strcmp(a1, "ON")) ? 1 : 0;
    } else if (!strcmp(sub, "RESET")) {
        r.type = REQ_RESET;
    } else {
        print_help();
        printf("ERR,unknown TROLLEY command '%s'\n", sub);
        return;
    }

    xSemaphoreTake(d.lock, portMAX_DELAY);
    const bool busy = d.req_count >= REQ_SLOTS;
    if (!busy) {
        d.req[(d.req_head + d.req_count) % REQ_SLOTS] = r;
        d.req_count++;
    }
    xSemaphoreGive(d.lock);
    if (busy) printf("ERR,TROLLEY,busy - %d TROLLEY commands already waiting\n", REQ_SLOTS);
}

void trolley_get_status(trolley_status_t *out)
{
    memset(out, 0, sizeof(*out));
    out->present = d.present;
    if (!d.present) {
        out->state = "OFF";
        return;
    }
    out->test_mode = d.st == ST_TEST;
    out->state = (d.st == ST_IDLE && d.waiting_reverse) ? "REVERSE_DWELL" : ST_NAME[d.st];
    out->dir = d.duty > 0.0f ? d.dir : 0;
    out->duty_pct = d.duty;
    out->speed_pct = (float)out->dir * d.duty;
    out->pos_mm = d.pos_mm;
    out->vel_mm_s = d.vel_mm_s;
    out->zeroed = d.zeroed;
    out->rail_limit = d.rail_limit;
    out->encoder_fault = d.encoder_fault;
    out->enc_total = trolley_io_encoder_total();
}

void trolley_print(const char *tag)
{
    trolley_status_t s;
    trolley_get_status(&s);
    printf("RIG,%s,state=%s,dir=%s,duty=%.1f,pwm=%uHz,in=%s,pos_mm=%.0f,vel_mm_s=%.0f,counts=%u,zeroed=%d,limits=%s,"
           "rail_limit=%d,enc_fault=%d,enc=%s\n",
           tag, s.state, s.dir > 0 ? "FWD" : (s.dir < 0 ? "REV" : "-"), s.duty_pct, (unsigned)trolley_io_pwm_hz(),
           trolley_io_in_name(trolley_io_in()), s.pos_mm, s.vel_mm_s, (unsigned)s.enc_total, s.zeroed,
           (d.cfg.soft_limits && d.limits_on) ? (s.zeroed ? "ARMED" : "WAIT_ZERO") : "OFF", s.rail_limit,
           s.encoder_fault, trolley_io_encoder_ready() ? "ok" : "FAILED");
}
