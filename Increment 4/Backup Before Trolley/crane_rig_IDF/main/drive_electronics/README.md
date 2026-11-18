# Drive electronics firmware (EEII 1 VFD, ICD IF6.0) — crane MCU v4.5.0

Added 17 Nov 2026. Everything new lives in this folder. Every assumed value is in **one place**: `main.c` → `start_drive_electronics()`. Each line there is marked **CONFIRMED** or **ASSUMED**.

| File | What it does |
|---|---|
| `drive_spwm.c/.h` | 3-phase SPWM on the MCPWM peripheral: **10 kHz carrier**, V/f amplitude, B/C swap for reverse, opto inversion, idle hold, fixed-duty square test mode |
| `drive_control.c/.h` | Hoist drive state machine (100 Hz task): dashboard `hoist_cmd` → SPWM + DRV_EN + brake; fault inputs; lost link; overload; `DRIVE …` console commands |
| `mcp_shared.c/.h` | One owner for the MCP23017 latches, so the nRF CE (GPA0), DRV_EN (GPA1) and the brake (GPB5) never overwrite each other |

## Pins

| Header | Signal | MCU | Dir | Level / behaviour |
|---|---|---|---|---|
| J7.8 | VFD_SPWM_A | GPIO38 | out | 10 kHz SPWM. **Pin is inverted** (the 4N25 inverts back) |
| J7.7 | VFD_SPWM_B | GPIO39 | out | lags A by 120° on UP |
| J7.6 | VFD_SPWM_C | GPIO40 | out | lags A by 240° on UP |
| J3.4 | DRV_EN_LOGIC | MCP GPA1 | out | HIGH = DC-bus IGBT on. LOW at boot, LOW whenever stopped |
| J4.8 | HOIST_BRAKE | MCP GPB5 | out | HIGH = release (assumed). LOW (clamped) at boot |
| J3.5 | DRV_FAULT_IN | MCP GPA2 | in | **assumed** active LOW, pull-up on → fault b6 |
| J3.8 | VFD_OC_IN | MCP GPA5 | in | **assumed** 10 A trip flag, active LOW, pull-up on → fault b0 |

While stopped, all three SPWM pins sit **HIGH** (IR2104 IN LOW = low-side IGBTs on). An unconnected input reads "no fault" because of the pull-up.

## How the dashboard drives it (no dashboard change needed)

The hoist slider sends `hoist_cmd` −100…+100 % (+ = up) at 20 Hz.

1. **Start:** `|cmd| ≥ 2 %` → DRV_EN HIGH and SPWM at 5 Hz. After 50 ms the brake releases → RUN.
2. **Run:** frequency ramps at 25 Hz/s to `|cmd| × 50 Hz`. Amplitude follows V/f: m = 0.70 × f / 50, minimum 0.15.
3. **Stop** (cmd 0): ramp down to 5 Hz → brake clamps → 50 ms → SPWM idle, DRV_EN LOW.
4. **Reverse** (EEII 1: stop first): full stop as above, at rest ≥ 500 ms, then restart with B and C swapped.
5. **Lost link** (no frame for 500 ms) or **Soft stop**: the same stop, but ramping at 100 Hz/s.
6. **Hold brake** flag → treated as hoist 0.
7. **Overload** (> 20 kg) → UP blocked, DOWN still allowed.
8. **DRV_FAULT / overcurrent input:** everything off at once (SPWM idle, DRV_EN LOW, brake clamped). The fault latches until the dashboard **Reset** button (Table 33 b1) or `DRIVE RESET`. After a reset the hoist must go back to 0 before it will start again.

**Telemetry back to the dashboard:**
- `hoist_vel` / `hoist_pos`: estimated from the drive frequency (80 mm/s at 50 Hz until MCTR 2 gives spool and gear data).
- `system_health`: b4 VFD enabled = DRV_EN; b3 brake clamped.
- `fault_flags`: b0 / b6.

## Console test commands (COM12) — for measuring one phase at a time

These work without the radio. While a test mode is on, dashboard hoist commands are ignored; `DRIVE STOP` hands control back.

| Command | Output |
|---|---|
| `DRIVE` | status line |
| `DRIVE SQUARE` | **the "yellow" signal**: 10 kHz, 50 % at the MCU pin, all three phases |
| `DRIVE SQUARE A` / `B` / `C` | the same on one phase only (the others held idle) |
| `DRIVE SQUARE B 30` | phase B, 30 % at the pin (the IR2104 IN sees 70 %) |
| `DRIVE SINE 20 UP` | 3-phase SPWM at 20 Hz (V/f amplitude), order A-B-C |
| `DRIVE SINE 20 DOWN` | same, A-C-B (from rest only: `DRIVE STOP` first) |
| `DRIVE EN 1` / `DRIVE EN 0` | DRV_EN J3.4 by hand (test mode only) |
| `DRIVE BRAKE RELEASE` / `CLAMP` | brake J4.8 by hand (test mode only) |
| `DRIVE STOP` | all idle, DRV_EN LOW, brake clamped, dashboard control back |
| `DRIVE RESET` | clear latched faults |

**Suggested bench order (HV off, scope on one phase at a time):**
1. `DRIVE SQUARE A` → 10.00 kHz, 50 %, 0/3.3 V on J7.8. Compare it with the signal-generator trace. Repeat for B and C.
2. `DRIVE SQUARE A 15`, then `DRIVE SQUARE A 85` → check that the opto output still makes a full pulse at both ends. This sets `duty_min` / `duty_max`.
3. `DRIVE SINE 5 UP`, then `DRIVE SINE 50 UP` → filtered (or averaged) duty traces a sine. Check two channels 120° apart.
4. `DRIVE STOP`, `DRIVE SINE 50 DOWN` → B and C swapped.
5. `DRIVE STOP` → from the dashboard, hoist up and down → watch `RIG,DRIVE,STATE,…` lines plus J3.4 and J4.8.

## Numbers

| Quantity | Calculation | Value |
|---|---|---|
| Carrier period | 1 / 10 kHz | 100 µs |
| Timer ticks per period | 40 MHz / 10 kHz | 4000 (25 ns resolution) |
| Samples per sine cycle at 50 Hz | 10 000 / 50 | 200 |
| Phase step at 50 Hz | 50 / 10 000 × 2³² | 21 474 836 |
| Duty swing at 50 Hz | 0.5 ± 0.70 × 0.5 | 15 % … 85 % (= the clamps, so the sine is never clipped) |
| Shortest pulse at the IR2104 IN | 15 % × 100 µs | 15 µs |
| Normal stop from 50 Hz | (50 − 5) / 25 + 0.05 | 1.85 s |
| Lost-link stop from 50 Hz | 0.5 s detection + (50 − 5) / 100 + 0.05 | ≈ 1.0 s |
| ISR load | 10 000 interrupts/s × ~3 compare writes | half the starter's 20 kHz |

## What changed outside this folder (all marked `DRIVE`)

- `main.c`: `start_drive_electronics()` (all assumptions), `mcp_shared_init()`, and the drive starts straight after the MCP23017 is probed.
- `rf_link.c`: CE writes through `mcp_shared`; every MOVE frame → `drive_on_move()`; `DRIVE` console command; `RIG,DRIVE,…` line in every heartbeat.
- `rig_telemetry.c/.h`: hoist fields follow the real drive; `rig_tlm_overloaded()` exported.
- `rig_config.h`: v4.5.0, `RIG_ENABLE_DRIVE 1` (the starter `RIG_ENABLE_SPWM` stays 0; enabling both is a compile error).
- `CMakeLists.txt`: the three sources plus the include folder.

## Verified here / still to verify on the board

- **Checked:**
  - Every new and changed file passes a host C syntax and type check against the ESP-IDF v6.1 MCPWM / GPIO / I2C signatures (`-Wall -Wextra`, clean).
  - A host simulation of `drive_spwm.c` + `drive_control.c` with a fake clock and peripherals confirmed: start/stop/brake timing, the 25 and 100 Hz/s ramps, the 500 ms reverse dwell, lost-link stop, fault latch and reset, overload, the test commands, the 15–85 % clamps, and phase order (UP A-B-C, DOWN A-C-B).
- **Not done:**
  - No real `idf.py build`: the Xtensa toolchain couldn't be downloaded in the cloud workspace. The first build on the PC is the compile check.
  - Nothing measured on hardware yet.
