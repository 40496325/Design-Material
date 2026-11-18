# Trolley firmware (MCTR 1 lateral movement, ICD IF1.0) — crane MCU v4.6.0

Added 17 Nov 2026. Everything new lives in this folder. All assumed values are in **one place**: `main.c` → `start_trolley()`, each marked **CONFIRMED** or **ASSUMED**.

**Pins come only from `MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx`** (the final pinout for every package).

| File | What it does |
|---|---|
| `trolley_io.c/.h` | Hardware: ENA PWM on J7.9 (LEDC, 5 kHz, 10-bit), IN1–IN4 on J4.4–J4.7 in one MCP23017 write, encoder on J7.12 (PCNT, rising edges) |
| `trolley_control.c/.h` | 100 Hz task: dashboard `trolley_cmd` → direction + duty, encoder position and speed, 20 m rail limits with a slow-down zone, lost link / soft stop, encoder check, `TROLLEY …` console commands |

It uses `drive_electronics/mcp_shared.c` (one owner for the MCP23017), so the radio CE, DRV_EN, the brake and IN1–IN4 never overwrite each other.

## Pins (wiring document, MCTR 1)

| Header | Signal | MCU | Dir | Behaviour |
|---|---|---|---|---|
| J7.9 (IO_A) | LAT_PWM_ENA | GPIO18 | out | 5 kHz PWM, duty = \|trolley_cmd\| %. 0 % at boot |
| J4.4 | MOT_IN1 | MCP GPB1 | out | HIGH on "+" (FWD) |
| J4.5 | MOT_IN2 | MCP GPB2 | out | HIGH on "−" (REV) |
| J4.6 / J4.7 | MOT_IN3 / IN4 | MCP GPB3 / GPB4 | out | copies of IN1 / IN2, so either HW-095 wiring works (ENB jumpered to ENA) |
| J7.12 (IO_D) | ENC_A | GPIO21 | in | rising edges counted by the PCNT; must arrive at ≤ 3.3 V |
| J4.2 / J4.1 | +5 V / GND | — | power | HW-095 logic + encoder supply |

At boot and whenever the trolley is stopped: ENA 0 %, IN1–IN4 LOW (L298N: EN low = free running).

## How the dashboard drives it (no dashboard change needed)

The trolley slider sends `trolley_cmd` −100 … +100 % at 20 Hz.

1. **Start:** |cmd| ≥ 2 % → IN1/IN2 set for the direction, ENA duty ramps at 100 %/s to |cmd| %.
2. **Stop** (cmd 0): duty ramps to 0, then IN1–IN4 LOW (coast).
3. **Reverse:** stop first, then wait until the encoder shows standstill (no counts for 100 ms, at least 300 ms after the stop, at most 3 s), then the other way. Counts that arrive while coasting keep the old direction's sign, so the position stays right.
4. **Lost link** (500 ms) / **Soft stop** / **rail limit**: the same stop, ramping at 250 %/s.
5. **Rail limits** (20 m rail, 0.2 m margin), armed after **Zero Position**:
   - Put the trolley at the end a **"−"** command drives it to, then press the dashboard's Zero Position button (Table 33 b3) or type `TROLLEY ZERO`.
   - **Armed:** no "+" beyond 19.8 m, no "−" below 0.2 m, and at most **30 % duty in the last 1 m** before either limit. Table 34 fault **b3** shows while a limit is stopping the trolley.
   - Which end is "+" does not matter, as long as you zero at the "−" end.
6. **Encoder check:** driving at ≥ 20 % for 1 s with no counts → Table 34 fault **b5** (indication only; motion is not stopped). It clears when counts return, or with Reset.

**Telemetry:** `trolley_pos` / `trolley_vel` in Table 34 come from the encoder, in both MODEL and ZERO telemetry modes.

## Console test commands (COM12, inside the serial monitor)

These work without the radio. While a test is running, dashboard trolley commands are ignored; `TROLLEY STOP` hands control back.

| Command | Output |
|---|---|
| `TROLLEY` | status: state, duty, IN state, position, speed, raw counts, limits, faults |
| `TROLLEY PWM` | J7.9 at 5 kHz, **50 %**, IN unchanged (LOW/LOW: the motor is **not driven**) — pure signal check |
| `TROLLEY PWM 25` | the same at 25 % |
| `TROLLEY RUN 30 FWD` | IN1 HIGH / IN2 LOW and 30 % → the motor turns ("+") |
| `TROLLEY RUN 30 REV` | IN1 LOW / IN2 HIGH ("−"). Changing direction needs `TROLLEY STOP` first |
| `TROLLEY IN FWD` / `REV` / `COAST` / `BRAKE` | IN1–IN4 only (for checking J4.4–J4.7 with a meter). BRAKE = both HIGH |
| `TROLLEY ZERO` | position = 0 mm, arms the rail limits |
| `TROLLEY LIMITS 0` / `1` | rail limits off / on |
| `TROLLEY RESET` | clear the encoder / rail-limit indications |
| `TROLLEY STOP` | ENA 0 %, IN1–IN4 LOW, dashboard control back |

**Suggested bench order (motor supply off first):**
1. `TROLLEY PWM` → scope J7.9: 5.00 kHz, 50 %, 0 / 3.3 V. Then `TROLLEY PWM 10` and `TROLLEY PWM 90`.
2. `TROLLEY IN FWD` → meter J4.4 = 3.3 V, J4.5 = 0 V (J4.6 / J4.7 the same). `TROLLEY IN REV` → swapped. `TROLLEY STOP`.
3. **Before connecting ENC_A:** measure the encoder output (this is the open MCTR 1 question). It must be ≤ 3.3 V at J7.12.
4. Motor supply on, wheels off the rail: `TROLLEY RUN 20 FWD` → wheels turn, `TROLLEY` shows counts rising. Turn the wheel 10 times by hand with `TROLLEY STOP` active → counts ≈ 10 500.
5. `TROLLEY STOP`, then drive from the dashboard.

## Numbers

| Quantity | Calculation | Value |
|---|---|---|
| Distance per count | π × 60 mm / 1050 | 0.1795 mm |
| Counts over the 20 m rail | 20 000 / 0.1795 | ≈ 111 400 |
| Encoder rate at 1 m/s (SysRS PT1) | 1000 / 0.1795 | ≈ 5.6 kHz (PCNT limit is MHz; glitch filter 1 µs) |
| Counts per 10 ms poll at 1 m/s | 5 570 × 0.01 | ≈ 56 (wrap point 30 000, so a wrap can't be missed) |
| ENA resolution | 10 bit at 5 kHz | 0.1 % |
| Normal ramp 0 → 100 % | 100 / 100 %/s | 1.0 s |
| Lost-link stop from 100 % | 0.5 s detection + 100 / 250 %/s | ≈ 0.9 s |
| Slow-zone speed (if 100 % ≈ 1 m/s) | 30 % | ≈ 0.3 m/s over the last 1 m |

## What changed outside this folder (all marked `TROLLEY`)

- `main.c`: `start_trolley()` (all assumptions) is called after the drive. The drive's optional digital overcurrent input on J3.8 is now **off** (J3.8 is a spare pin in the wiring document).
- `rf_link.c`: every MOVE frame → `trolley_on_move()`; `TROLLEY` console command; `RIG,TROLLEY,…` in every heartbeat.
- `rig_telemetry.c`: trolley position / speed / faults come from the encoder.
- `rig_config.h`: v4.6.0, `RIG_ENABLE_TROLLEY 1`.
- `CMakeLists.txt`: the two sources, the include folder, `esp_driver_ledc` and `esp_driver_pcnt`.
- `drive_electronics/mcp_shared.c/.h`: `mcp_shared_write_mask()`, so IN1–IN4 change in one write.

## Verified here / still to verify on the board

- **Checked:**
  - Host C syntax and type check of every file against the ESP-IDF v6.1 LEDC / PCNT / GPIO signatures (`-Wall -Wextra`, clean).
  - Host simulation (fake LEDC / PCNT / MCP23017, simple trolley model) passed: pins and IN3/IN4 copy, ramps, stop, standstill-before-reverse, lost link, Zero Position + limits + slow zone (stops inside the margin from full speed), encoder fault and recovery, all console commands, position bookkeeping while braking.
  - The drive simulation was re-run after the shared changes and still passes.
- **Not done:**
  - The board build (`idf.py build`) and any measurement on hardware.
  - The encoder output type is still open with MCTR 1: measure it before connecting J7.12.
