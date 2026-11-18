# Magnet firmware (EEII 2 load attachment, ICD IF12.0) — crane MCU v4.7.0

Added 18 Nov 2026. Everything new lives in this folder, plus a small shared-ADC hook in `loadcell.c`. All assumed values are in **one place**: `main.c` → `start_magnet()`, each marked **CONFIRMED** or **ASSUMED**.

**Pins come only from `MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx`** (the final pinout).

> **Final pass (18 Nov evening).** EEII 2 confirmed: magnet supply **12 V**, **99 %** stays on 12 V, holding current **7.5 A**, coil resistance **≈ 1 Ω**, and the 5 V bench supply in the video was **current-limited** (so 2.53 A was the supply's limit, not what the coil would draw). Any further change is made only in `main.c` → `start_magnet()`.
>
> On 12 V: 12 × 0.99 / ~1.04 Ω (coil + switch + wiring) ≈ **11.4 A** (up to 11.9 A), ≈ **140 W** in the coils — about 1.5× the ICD 7.5 A. The firmware warns above 10 A; see *Problems to expect* below.

| File | What it does |
|---|---|
| `magnet_control.c/.h` | MAG_PWM on J7.5 (LEDC timer 1, 15 kHz, 10-bit), MAG_LOGIC on J3.6, engage / release sequence, coil current from J6.8, holding status and fault, `MAGNET …` console commands |
| `../loadcell.c` (hook, marked `MAGNET`) | `loadcell_set_aux()` / `loadcell_get_aux()`: the load-cell task shares the ADS1220 and reads AIN3 every 500 ms |

## Pins (wiring document, EEII 2)

| Header | Signal | MCU | Dir | Behaviour |
|---|---|---|---|---|
| J7.5 (silkscreen IO4) | MAG_PWM | **GPIO41** | out | 15 kHz. ON 99 % (12 V supply), release 10 %, boot 0 % |
| J3.6 | MAG_LOGIC | MCP23017 GPA3 | out | enable: HIGH while engaged, LOW at boot and when released |
| J6.8 (AN3) / J6.9 | MAG_CURR_IN | ADS1220 AIN3 | in | current sensor output, 0–3.3 V (1 kΩ + 2.2 nF on board) |

**"IO4" is the breakout's silkscreen number, not GPIO4.** The board map gives J7.3–J7.8 = GPIO 1, 42, **41**, 40, 39, 38. GPIO4 is J7.11 (IO_C, the trolley's spare encoder pin).

## How the dashboard drives it (no dashboard change needed)

The magnet button sends `magnet_state` (Table 33 byte 6: 0 off, 1 on) at 20 Hz.

1. **ON:** MAG_LOGIC HIGH, then 5 ms later the PWM goes to 99 %.
2. **OFF:** the PWM goes to 10 % (EEII 2: disengages, 0.00 A measured), then MAG_LOGIC LOW.
3. **Boot:** PWM 0 %, MAG_LOGIC LOW, until the first command.
4. **Lost link / soft stop:** the magnet **keeps its state** (ICD safe hold — never drop the load). Only an explicit OFF releases it.
5. **Telemetry (Table 34):**
   - `magnet_current` = |I| × 10 (byte 14).
   - `magnet_status` (byte 15): 0 off, 1 holding, 2 fault.
   - Fault **b4** when the magnet is engaged but the current is still below the holding threshold 2 s later, or when the sensor reading is implausible. **The firmware never switches the magnet off because of a fault.**

## Current measurement

The ADS1220 is shared with the load cell. Every 500 ms the load-cell task:
1. switches to AIN3 against ground (gain 1, PGA bypassed, reference = AVDD);
2. takes two conversions, keeping the second;
3. restores the bridge setup;
4. throws away the next bridge sample.

```
V = code / 2^23 × AVDD                     (AVDD = 3.30 V, assumed)
I = (V − V_zero) / S                        S = −0.0988 V/A (from EEII 2's measurement)
```

**Calibration from EEII 2's bench test (18 Nov, video):**

| Reading | Duty | Coil current (supply meter) | Sensor |
|---|---|---|---|
| release | 10 % | 0.00 A | 2.57 V |
| engaged | 99 % | 2.53 A | 2.32 V |

S = (2.32 − 2.57) V / 2.53 A = **−0.0988 V/A**.

The output **falls** as current rises, so the sensor is fitted "backwards". That is why no divider is needed: at the full 7.5 A it reads 2.57 − 0.0988 × 7.5 ≈ **1.83 V**, well inside 0–3.3 V.

Note: the datasheet graph EEII 2 sent has a slope of about **+0.185 V/A** around 2.5 V (0.65 V at −10 A, 4.4 V at +10 A). The measured slope is about half that. Check it with `MAGNET CAL` (below).

- **Auto-zero:** while the magnet has been released for 2 s, `V_zero` slowly follows the sensor output (it starts at 2.57 V).
- **Plausibility:** readings outside 0.3–3.2 V (sensor unplugged or unpowered) are treated as a sensor fault, not as current. Without this, 0 V would read as about +26 A, i.e. "holding".

## Console test commands (COM12, inside the serial monitor)

These work without the radio. While a test is running, dashboard magnet commands are ignored. After `MAGNET STOP` the magnet follows the dashboard again straight away: if the dashboard still says ON, it re-engages.

| Command | Output |
|---|---|
| `MAGNET` | status: duty, J3.6 level, sensor V, zero V, current, holding status, faults |
| `MAGNET PWM` / `MAGNET PWM 30` | J7.5 at 15 kHz, 50 % (or 30 %). MAG_LOGIC unchanged — pure signal check |
| `MAGNET LOGIC 1` / `0` | J3.6 HIGH / LOW only |
| `MAGNET ON` / `MAGNET OFF` | the full engage / release sequence |
| `MAGNET ZERO` | take the present sensor voltage as 0 A (magnet released) |
| `MAGNET CAL 2.53` | while ON: sets the slope from the current flowing now (e.g. the supply's meter). Prints the value to put in `main.c` |
| `MAGNET THRESHOLD 2` | holding threshold until reboot. The `main.c` default is 7.5 A; use about 2 A on the 5 V bench |
| `MAGNET RESET` | clear the fault |
| `MAGNET STOP` | release (10 %, J3.6 LOW), dashboard control back |

**Suggested bench order:**
1. Magnet supply off. `MAGNET PWM` → scope J7.5: 15.00 kHz, 50 %, 0 / 3.3 V. Then `MAGNET PWM 99` and `MAGNET PWM 10`.
2. `MAGNET LOGIC 1` → J3.6 = 3.3 V; `MAGNET LOGIC 0` → 0 V.
3. Sensor powered, magnet supply off: `MAGNET` → `sensor_v` about 2.5–2.6 V. Then `MAGNET ZERO`.
4. 12 V supply (rated ≥ 12 A), clamp meter on a coil lead: `MAGNET ON` → ≈ 11 A. `MAGNET CAL <clamp amps>`, copy the printed V/A into `main.c`. Status **holding** (1); expect `over_current=1` above 10 A. Feel / measure the coil temperature over a few minutes. `MAGNET OFF`.
5. Released: `MAGNET` → `release_i_high` must be 0 and the plate must drop. If not, `release_duty_pct = 0.0f`.
6. `MAGNET STOP`, then the dashboard magnet button: holding (1) on, 0 off.
(On a current-limited bench supply use `MAGNET THRESHOLD 2`.)

## Warnings (console + `MAGNET` status flags; nothing is ever switched)

| Flag | When | Meaning / what to do |
|---|---|---|
| `release_i_high=1` | released ≥ 2 s and \|I\| > 0.5 A | the 10 % release duty still drives current (≈ 1.2 A on 12 V) — the plate may stay stuck. Set `release_duty_pct = 0.0f` |
| `over_current=1` | engaged and \|I\| > 10 A | above the ICD 7.5 A by a margin — check coil temperature and the 12 V supply's rating; ≈ 65 % duty gives ≈ 7.7 A |
| fault b4, status 2 | engaged, \|I\| < 7.5 A after 2 s, or sensor outside 0.3–3.2 V | not holding properly (weak / current-limited supply, broken coil, sensor clipping or unplugged) |

Auto-zero only follows drift smaller than 0.05 V (≈ 0.5 A), so a real current at the release duty is shown instead of being zeroed away.

## Numbers

| Quantity | Calculation | Value |
|---|---|---|
| PWM period | 1 / 15 kHz | 66.7 µs |
| ON pulse at 99 % / release pulse at 10 % | 0.99 × 66.7 / 0.10 × 66.7 | 66.0 µs / 6.7 µs |
| PWM resolution | 10 bit at 15 kHz | 0.1 % |
| Sensor slope (bench) | (2.32 − 2.57) / 2.53 | −0.0988 V/A (≈ ACS712-20A, 100 mV/A) |
| Coil current on 12 V at 99 % | 12 × 0.99 / 1.04 Ω | ≈ 11.4 A (11.9 A at exactly 1 Ω) |
| Coil power on 12 V at 99 % | 11.4² × 1.0 | ≈ 130–140 W |
| Duty for the ICD 7.5 A on 12 V | 7.5 × 1.04 / 12 | ≈ 65 % |
| Coil current at 10 % release on 12 V | 12 × 0.10 / 1.04 | ≈ 1.15 A |
| Sensor at 7.5 A / 11.4 A | 2.57 − 0.0988 × I | 1.83 V / 1.44 V (inside 0–3.3 V) |
| Telemetry byte 14 at 11.4 A | 11.4 × 10 | 114 (max 255 = 25.5 A) |
| Current update rate | one reading per ~0.5 s | ≈ 2 /s |
| Load-cell rate with sharing | 20 SPS minus 3 conversions per 0.5 s | ≈ 12–14 /s (was 20) |

## Problems to expect from the bench test in the video

1. **The calibration point is only 2.53 A** because the bench supply was current-limited. The slope is extrapolated to 7.5–11.4 A. Fix: on the 12 V supply, `MAGNET ON`, read the true current with a clamp meter (or the 12 V supply's meter, if it is **not** limiting), `MAGNET CAL <amps>`.
2. **Which sensor is fitted?** −0.0988 V/A matches a **±20 A** ACS712-type (100 mV/A). The graph EEII 2 sent (≈ 185 mV/A) is the **±5 A** version. If the ±5 A part is fitted it clips near 5 A, and 11 A coils would read wrong (simulated: 11.4 A reads as 9.3 A with the bench slope — it still looks "holding"). Test: `MAGNET CAL` at full current. If the new slope differs from −0.0988 by more than ~10 %, the sensor is clipping or non-linear.
3. **"10 % disengages, 0.00 A" was measured on the current-limited 5 V supply.** On 12 V with ~1 Ω coils, 10 % is ≈ 1.2 V → ≈ 1.15 A of holding current. A light plate may not drop. Watch `release_i_high`; if it shows 1 or the load stays stuck, set `release_duty_pct = 0.0f`.
4. **Heat.** 99 % on 12 V with ~1 Ω is ≈ 11–12 A, ≈ 140 W — about 1.5× the ICD current and ~2.4× its power. Coil resistance rises ~0.4 %/°C, so the current drops slowly as it warms. Time how long the coil can stay on before it is too hot; if needed, lower `on_duty_pct` (≈ 65 % = ICD 7.5 A).
5. **12 V supply rating.** It must deliver ≈ 12 A continuously. A supply that current-limits below 7.5 A gives fault b4 (simulated with a 6 A limit) while the magnet is on.
6. **Turn-off spike.** Dropping ≈ 11 A in an inductive coil needs a freewheel diode across the coil on EEII 2's module. Ask EEII 2 to confirm it is fitted.
7. **The 2.7 V / 2.5 V readings** EEII 2 quoted earlier don't match 2.57 V / 2.32 V. Auto-zero handles a different zero; it does not fix a different slope — `MAGNET CAL` does.
8. **Sensor zero 2.57 V instead of the nominal 2.50 V.** A ratiometric 5 V sensor shows this if its supply is ≈ 5.14 V. If the sensor's 5 V supply on the crane differs from the bench, the zero moves (auto-zero handles it) and the slope changes by the same ratio (re-check with `MAGNET CAL`).

## What changed outside this folder (all marked `MAGNET`)

- `main.c`: `start_magnet()` (all assumptions), called after the trolley.
- `loadcell.c/.h`: shared-ADC side channel (`loadcell_set_aux` / `loadcell_get_aux`) — README C4.
- `rf_link.c`: every MOVE frame → `magnet_on_move()`; `MAGNET` console command; `RIG,MAGNET,…` in every heartbeat.
- `rig_telemetry.c`: magnet current, status and fault b4 are real (both telemetry modes).
- `rig_config.h`: v4.7.0, `RIG_ENABLE_MAGNET 1`.
- `CMakeLists.txt`: `magnet/magnet_control.c` and the include folder.

## Verified here / still to verify on the board

- **Checked:**
  - Host C syntax / type check of every file against the ESP-IDF v6.1 signatures (`-Wall -Wextra`, clean).
  - Host simulation of `magnet_control.c` (fake LEDC / MCP23017 and a sensor model from EEII 2's numbers): pins, boot state, auto-zero, engage / release order, 2 s holding fault without switching off, threshold change, lost link keeps the magnet on, all console commands, CAL recovering a 0.185 V/A sensor, unplugged sensor → fault (not "holding"). Final 12 V model (1.04 Ω): 11.4 A → holding + over-current warning; 10 % release → 1.15 A shown (not zeroed away) + release warning; release 0 % → 0 A; 12 V supply limited at 6 A → fault b4 with the magnet kept on; ±5 A sensor clipping → reads 9.3 A, CAL at full current exposes it.
  - Simulation of the real `loadcell.c` with a fake ADS1220: never read in the wrong mode, every first bridge sample after a switch discarded, current read ~2× per second.
- **Not done:**
  - The board build and any hardware measurement.
  - Calibration at full current, the release at 10 %, and coil heating on 12 V (*Problems to expect*).
