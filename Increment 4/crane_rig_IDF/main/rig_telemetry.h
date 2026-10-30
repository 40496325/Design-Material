// =============================================================================
// rig_telemetry.h - fills the ICD Table 34 uplink frame (0x81) on the crane
// =============================================================================
// Two telemetry sources, switchable at runtime (console: TLM MODEL / TLM ZERO):
//
//   MODEL (Increment 4 bench default)
//     A loopback model driven by the Table 33 commands, so every downlink field
//     produces a visible uplink field on the dashboard before the MCTR / EEII
//     sensors exist. This is the "pack/unpack loopback across both ends"
//     evidence the progress record asks for. Positions, speeds, currents and
//     brake/VFD flags are NOT measured crane quantities.
//
//   ZERO
//     Only what this firmware really knows: link OK / wireless timeout.
//     Unmeasured fields are 0.
//
//   In BOTH modes the load cell (bytes 10-11) is the real ADS1220 measurement
//   from loadcell.c, and the > 20.0 kg overload fault + lift inhibit use it.
//
// Model rules (all placeholders until MCTR/EEII confirm, see rig_config.h):
//   trolley_vel = trolley_cmd % x RIG_MODEL_TROLLEY_MAX_MM_S   (SysRS PT1 >= 1 m/s)
//   hoist_vel   = hoist_cmd % x RIG_MODEL_HOIST_MAX_MM_S       (SysRS PT2 1.6 m / 20 s)
//   positions integrate and clamp to rail length / hoist stroke (rail limit fault)
//   soft stop, stale command (> RIG_LINK_TIMEOUT_MS) or lost link -> speeds 0 (ICD Safe Hold)
//   hoist UP inhibited while load > 20.0 kg (ICD IF13.0 Overload Inhibit)
//   brake clamped when the hoist is not commanded or HOLD BRAKE is set
//   magnet follows magnet_state and stays on through a lost link (ICD)
//   wireless timeout fault latches until a RESET flag is received
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "icd_frames.h"

#ifdef __cplusplus
extern "C" {
#endif

void rig_tlm_init(bool model);
void rig_tlm_set_model(bool model);
bool rig_tlm_is_model(void);

/** A Table 33 MOVE frame arrived (call from the radio task). */
void rig_tlm_on_move(const icd_downlink_t *cmd, int64_t now_us);

/** Integrate the model; call every ~20 ms from the radio task. */
void rig_tlm_tick(int64_t now_us, bool link_ok);

/** Build the 32-byte uplink frame for the next ACK payload. */
void rig_tlm_build(icd_uplink_t *out, uint8_t seq_ack, bool link_ok);

/** Latest load-cell reading (called from the load-cell task, loadcell.c). */
void rig_tlm_set_load(float kg, bool valid, bool saturated);

/** One machine-readable line: RIG,TLM,... */
void rig_tlm_print(void);

#ifdef __cplusplus
}
#endif
