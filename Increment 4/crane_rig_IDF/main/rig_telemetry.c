#include "rig_telemetry.h"

#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rig_config.h"

typedef struct {
    bool model;
    // last MOVE command
    icd_downlink_t cmd;
    int64_t cmd_us;
    bool have_cmd;
    // model state
    float trolley_pos_mm, hoist_pos_mm;
    float trolley_vel_mm_s, hoist_vel_mm_s;
    int8_t trolley_eff, hoist_eff;     // % actually applied after soft stop / stale / interlocks
    bool brake_clamped, vfd_enabled, magnet_on, auto_mode;
    bool rail_limit, overload, timeout_latched, was_linked;
    int64_t last_tick_us;
} tlm_state_t;

static tlm_state_t t;
// Written by the load-cell task, read by the radio task (single 32-bit stores).
static volatile float s_load_kg;
static volatile bool s_load_valid;
static volatile bool s_load_saturated;

void rig_tlm_init(bool model)
{
    memset(&t, 0, sizeof(t));
    t.model = model;
    t.brake_clamped = true;
}

void rig_tlm_set_model(bool model) { t.model = model; }
bool rig_tlm_is_model(void) { return t.model; }

void rig_tlm_set_load(float kg, bool valid, bool saturated)
{
    s_load_kg = kg;
    s_load_saturated = saturated;
    s_load_valid = valid;
}

// Table 34 bytes 10-11, kg x 10. Real measurement in BOTH telemetry modes.
// 0xFFFF = ADC at its limit (wiring fault / gross overload) - reads as overload.
static uint16_t load_x10(void)
{
    if (!s_load_valid) return 0;
    if (s_load_saturated) return 0xFFFF;
    const float x10 = s_load_kg * 10.0f;
    if (x10 <= 0.0f) return 0;
    if (x10 >= 65534.0f) return 65534;
    return (uint16_t)lroundf(x10);
}

static bool overloaded(void)
{
    if (!s_load_valid) return false;
    return s_load_saturated || load_x10() > RIG_OVERLOAD_X10;
}

void rig_tlm_on_move(const icd_downlink_t *cmd, int64_t now_us)
{
    t.cmd = *cmd;
    t.cmd_us = now_us;
    t.have_cmd = true;
    if (cmd->mode_flags & ICD_MODE_RESET) {
        t.timeout_latched = false;            // operator acknowledged the faults
    }
    if (cmd->mode_flags & ICD_MODE_ZERO_POS) {
        t.trolley_pos_mm = 0.0f;
        t.hoist_pos_mm = 0.0f;
    }
}

static int8_t clamp_pct(int v)
{
    if (v > 100) return 100;
    if (v < -100) return -100;
    return (int8_t)v;
}

void rig_tlm_tick(int64_t now_us, bool link_ok)
{
    float dt = t.last_tick_us ? (float)(now_us - t.last_tick_us) / 1e6f : 0.0f;
    if (dt > 0.2f) dt = 0.2f;
    t.last_tick_us = now_us;

    if (link_ok) {
        t.was_linked = true;
    } else if (t.was_linked) {
        t.timeout_latched = true;             // ICD Table 34 fault b7
    }

    const bool fresh = t.have_cmd && link_ok && (now_us - t.cmd_us) < (int64_t)RIG_LINK_TIMEOUT_MS * 1000;
    const bool soft_stop = (t.cmd.mode_flags & ICD_MODE_SOFT_STOP) != 0;
    t.overload = overloaded();

    int trolley = (fresh && !soft_stop) ? t.cmd.trolley_cmd : 0;
    int hoist = (fresh && !soft_stop) ? t.cmd.hoist_cmd : 0;
    if (t.overload && hoist > 0) hoist = 0;   // Overload Inhibit: no lifting above 20 kg
    t.trolley_eff = clamp_pct(trolley);
    t.hoist_eff = clamp_pct(hoist);

    t.auto_mode = (t.cmd.mode_flags & ICD_MODE_AUTO) != 0;
    t.brake_clamped = (t.cmd.mode_flags & ICD_MODE_HOLD_BRAKE) || t.hoist_eff == 0;
    if (t.brake_clamped) t.hoist_eff = 0;
    t.vfd_enabled = t.hoist_eff != 0;
    if (t.have_cmd) t.magnet_on = t.cmd.magnet_state != 0;   // kept on through a lost link

    t.trolley_vel_mm_s = (float)t.trolley_eff * (float)RIG_MODEL_TROLLEY_MAX_MM_S / 100.0f;
    t.hoist_vel_mm_s = (float)t.hoist_eff * (float)RIG_MODEL_HOIST_MAX_MM_S / 100.0f;
    t.trolley_pos_mm += t.trolley_vel_mm_s * dt;
    t.hoist_pos_mm += t.hoist_vel_mm_s * dt;

    t.rail_limit = false;
    if (t.trolley_pos_mm <= 0.0f) {
        t.trolley_pos_mm = 0.0f;
        if (t.trolley_eff < 0) t.rail_limit = true;
    } else if (t.trolley_pos_mm >= (float)RIG_MODEL_RAIL_MM) {
        t.trolley_pos_mm = (float)RIG_MODEL_RAIL_MM;
        if (t.trolley_eff > 0) t.rail_limit = true;
    }
    if (t.rail_limit) t.trolley_vel_mm_s = 0.0f;
    if (t.hoist_pos_mm <= 0.0f) {
        t.hoist_pos_mm = 0.0f;
        if (t.hoist_vel_mm_s < 0) t.hoist_vel_mm_s = 0.0f;
    } else if (t.hoist_pos_mm >= (float)RIG_MODEL_HOIST_STROKE_MM) {
        t.hoist_pos_mm = (float)RIG_MODEL_HOIST_STROKE_MM;
        if (t.hoist_vel_mm_s > 0) t.hoist_vel_mm_s = 0.0f;
    }
}

static uint8_t error_code_from(uint8_t faults)
{
    // 0 = OK, otherwise 1 + index of the lowest active fault bit (Table 34 b0..b7)
    for (int b = 0; b < 8; b++) {
        if (faults & (1u << b)) return (uint8_t)(b + 1);
    }
    return 0;
}

void rig_tlm_build(icd_uplink_t *out, uint8_t seq_ack, bool link_ok)
{
    memset(out, 0, sizeof(*out));
    out->magic = ICD_MAGIC_TLM;
    out->seq_ack = seq_ack;

    uint8_t faults = 0;
    if (t.timeout_latched || !link_ok) faults |= ICD_FAULT_WIRELESS_TIMEOUT;
    uint8_t health = link_ok ? ICD_HEALTH_LINK_OK : 0;

    if (t.model) {
        out->trolley_pos = (int16_t)lroundf(t.trolley_pos_mm);
        out->hoist_pos = (int16_t)lroundf(t.hoist_pos_mm);
        out->trolley_vel = (int16_t)lroundf(t.trolley_vel_mm_s);
        out->hoist_vel = (int16_t)lroundf(t.hoist_vel_mm_s);
        out->vfd_current = (uint16_t)(abs(t.hoist_eff) * RIG_MODEL_VFD_FULL_X10 / 100);
        out->magnet_current = t.magnet_on ? RIG_MODEL_MAGNET_X10 : 0;
        out->magnet_status = t.magnet_on ? 1 : 0;
        if (t.rail_limit) faults |= ICD_FAULT_RAIL_LIMIT;
        if (t.auto_mode) health |= ICD_HEALTH_AUTO_MODE;
        if (t.brake_clamped) health |= ICD_HEALTH_BRAKE_CLAMP;
        if (t.vfd_enabled) health |= ICD_HEALTH_VFD_ENABLED;
    }
    // Load cell: a real measurement, so it is sent (and interlocked) in MODEL and ZERO alike.
    out->load_cell = load_x10();
    if (overloaded()) faults |= ICD_FAULT_OVERLOAD;
    out->fault_flags = faults;
    out->error_code = error_code_from(faults);
    out->system_health = health;
}

void rig_tlm_print(void)
{
    printf("RIG,TLM,source=%s,load_src=%s,load_kg=%.2f,load_x10=%u,trolley_pos=%d,hoist_pos=%d,trolley_vel=%d,"
           "hoist_vel=%d,magnet=%d,brake=%d,vfd=%d,overload=%d,rail_limit=%d,timeout_latched=%d\n",
           t.model ? "MODEL" : "ZERO", s_load_valid ? (s_load_saturated ? "CELL_SATURATED" : "CELL") : "NONE",
           s_load_valid ? s_load_kg : 0.0f, load_x10(), (int)lroundf(t.trolley_pos_mm), (int)lroundf(t.hoist_pos_mm),
           (int)lroundf(t.trolley_vel_mm_s), (int)lroundf(t.hoist_vel_mm_s), t.magnet_on,
           t.brake_clamped, t.vfd_enabled, overloaded(), t.rail_limit, t.timeout_latched);
}
