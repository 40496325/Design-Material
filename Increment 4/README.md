# REII Increment 4 — Remote Control Dashboard, RF Ground Station and Crane MCU (Group 13)

This folder holds everything REII needs for the final demonstration (Fri 13 Nov 2026):

* **Ground station:** NodeMCU-32S + nRF24L01+, on the operator PC's USB.
* **Crane MCU firmware:** ESP32-S3 main board. Per ICD-001 the MCU side of every package's interface is REII's.
* **PC dashboard:** Python backend + web page.
* **Test tooling:** produces the evidence for the report.

Plan, dates and milestones: `REII Progress/progress_and_milestones.md`. Hours: `REII Progress/REII_Timesheet_Group_13_Updated.xlsx`.

Everything here follows, in order of authority:

1. **ICD-001 Rev A** (signed Group 13 report, 23/24 Oct): `Increment 3/Interface Control Documentation/(ICD v1) Engineering Design 2026 Group 13 Report.pdf`
2. **MCU wiring and header pin allocation**: `NEWEST main control board firmware/MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx`
3. **SysRS v1.0** + **REII SoW**: `Eng Package Requirements/`

---

## Status at a glance (29 Oct 2026)

| Part | Done | Still to do (section) |
|---|---|---|
| **Ground station** | Firmware v4.1.0: ICD Normal mode + diagnostic mode, both payload profiles, raw-frame bridge for the dashboard, RF diagnostics. Bench link verified | Rail hardware fit-out (G1–G2); OTA channel change only if time allows (G3) |
| **RF link IF10.0** | 200/200 PINGs, 0.0 % PER, max RTT 0.84 ms, 200/200 ACKs with valid Table 34 telemetry, **CE on J3.3** (run `results/i4_bench/20261028_230328`) | Full-power test at 5–10 m on the rail with drives running |
| **Crane MCU** | Firmware v4.4.1: radio on J1 + CE J3.3, ICD start-up sequence, Table 33 decode, Table 34 ACK payload, load cell on J6 (real), console + self-tests | **All actuator and sensor interfaces**: lateral, brake, magnet, hoist SPWM, currents, encoder, plus the safety state machine (C1–C8) |
| **Load cell IF13.0** | ADS1220 on J6, gain 128, tare at boot, `LOAD CAL`, > 20 kg overload fault + lift inhibit, live on the dashboard | Calibration with known masses on the real hoist (6 Nov) |
| **Dashboard** | v4: 20 Hz Table 33 loop, every Table 34 field shown, faults / health lamps, S-curve ramps, deadman, soft stop, live load with tare / calibrate, PA and channel switching, CSV evidence — see `dashboard/README.md` | Background-tab heartbeat fix, telemetry source badges, PA MAX in the UI, auto mode + obstacle planner, ZV anti-sway (B1–B6) |
| **Anti-sway (SoW 4)** | Jerk-limited S-curve baseline (`dashboard/ramp.py`) | Pendulum period from rope length, ZV shaper, flat-vs-shaped test on the rail |
| **Obstacle avoidance (SoW 5)** | — | Auto-mode planner (B4) + crane segment execution |
| **Validation (SoW 6)** | Bench smoke tests, dashboard sessions | Table 43 stage 4 (unpowered handshake), rail tests, lost-link / E-stop / overload acceptance |

---

## Folder layout

```
Increment 4/
├── crane_rig_IDF/            ESP32-S3 crane board (COM12), ESP-IDF v6.1
│   └── main/
│       ├── rig_config.h      every pin and setting (change things here only)
│       ├── main.c            starter main.c + ICD start-up sequence
│       ├── nrf24.c/.h        nRF24L01+ register driver (spi_master)
│       ├── rf_link.c/.h      radio task: PRX on 'REII2', ACK-payload telemetry, console, self-tests
│       ├── rig_telemetry.c   builds the Table 34 reply (bench MODEL / ZERO + real load cell)
│       ├── loadcell.c        load cell on J6 via ads1220.c: tare, calibration, kg × 10
│       ├── icd_frames.h      Table 33 / 34 structs (static_assert 32 bytes)
│       └── mcp23017.c, spwm.c, sdcard.c, status_led.c, oled.c   starter drivers
├── groundstation_PIO/        NodeMCU-32S ground station (COM10), PlatformIO + RF24 1.6.x
│   ├── src/main.cpp
│   └── include/gs_config.h, icd_frames.h
├── dashboard/                PC dashboard (see dashboard/README.md)
├── tools/smoke_test.py       gates + 200-PING smoke test + RF diagnosis → smoke.csv
├── results/i4_bench/         one folder per smoke-test run
├── results/dashboard/        one folder per dashboard session (telemetry.csv, events.log)
└── Backup */                 frozen copies of working states (smoke test, load cell, channel switching)
```

---

## Locked decisions

These replace every open question in the older Increment 3 / REII Progress markdown files (open-question lists, action lists, the D-01…D-07 "proposed" items). Each decision is taken from the signed ICD or the wiring document. Where the ICD gives no number, a value is chosen and fixed here.

| # | Decision | Basis | Closes |
|---|---|---|---|
| D1 | Ground station = **NodeMCU-32S (ESP32)**, powered from the PC's USB, drawing nothing from the crane. The STM32F411 dongle is dropped | ICD 3.5.1, Table 30 | STM32 dongle plan, D-05 |
| D2 | Normal Crane Control Mode: **ch 77 (2477 MHz), 2 Mbps, CRC-16, 5-byte addresses, crane listens on `REII2`, GS on `REII1`, ARC 5, ARD 500 µs**. Diagnostic mode: ch 80, 250 kbps, ARC 15, ARD 1500 µs | ICD Tables 28, 32 | ARC 5 vs 15 conflict, D-00a/c |
| D3 | **Table 33 / Table 34 byte maps are frozen as printed in ICD Rev A.** The operating profile is `ICD` (dynamic payload length + 32-byte ACK payload). `STATIC` is kept only as a bring-up / diagnostic profile | ICD Tables 32–34 | D-01 "payload freeze", D-02 |
| D4 | **No application-level ARQ.** Hardware retries only; a failed command is dropped, never replayed | ICD 3.5.3 | FFBD "handshake" question |
| D5 | **nRF CE on J3.3 (MCP23017 GPA0)**, driven HIGH after `PRIM_RX`, re-checked every heartbeat. J7.10 is not used | ICD Table 31; verified run `20261028_230328` | CE pin moves, D-06 radio pins |
| D6 | Radio power **LOW (−12 dBm) on the bench (< 2 m)**, **MAX (0 dBm) on the rail (5–10 m)**. MCTR 1 estimates the rail at **20–30 m** (29 Oct), so with the operator at one end the crane can be up to ~30 m away: the rail RF test (G2) also covers the far end | ICD Table 29; MCTR 1 29 Oct | — |
| D7 | Command rate **20 Hz** (dashboard `--rate` up to 50) | ICD Table 32 (20–50 updates/s) | — |
| D8 | **Lost link = no valid command for 500 ms** (10 periods at 20 Hz) → **Safe Hold**: trolley and hoist to 0, brake clamped, VFD disabled, **magnet left as it is**. Fault b7 latches until Reset | ICD Table 28 mode 4, Table 32, operating sequence | freshness / watchdog "Q10 TBD" |
| D9 | Load cell on **J6 (AIN0–AIN1) through the on-board ADS1220**, gain 128, AVDD (excitation) as reference. The HX711 option is retired: J3.7 / J3.8 stay free per the wiring doc, and the HX711 alternative clashed with ENC_B on GPIO4 | ICD Table 38 primary path, wiring doc J3 | ADS1220 vs HX711 (SysRS 5.3) |
| D10 | **One ADS1220 shared by two inputs.** Load cell (AIN0–1, gain 128) and magnet current (AIN3 single-ended, PGA bypassed), both with the AVDD reference so the ICD's 0–3.3 V range fits. 90 SPS, sequence L, L, M → load cell ≈ 60 S/s (averaged over 100 ms), magnet current ≈ 30 S/s. The **VFD current is not read on J6.7 (AIN2)**: EEII 1 confirmed on 29 Oct that their current sensor has a DAC and needs a **digital input**. Pin and signal format to be agreed (ICD Rev B) | ICD Tables 19, 26, 39 (20–50 Hz); EEII 1 29 Oct | ADC channel sharing |
| D11 | **Lateral (IF1.0).** **One HW-095 drives both motors** from one set of MCU signals, as in the ICD: LEDC PWM on **J7.9 = GPIO18 at 5 kHz** to ENA (ENB jumpered to ENA on the driver); IN1 / IN2 on **J4.4 / J4.5**; the firmware drives IN3 / IN4 (J4.6 / J4.7) as a copy of IN1 / IN2, so MCTR 1 can either wire them there or jumper IN3/IN4 to IN1/IN2 on the driver. Both driver channels are used, one motor each, which keeps each channel within its current rating. Duty = \|`trolley_cmd`\| %. **Encoder ENC_A on J7.12 = GPIO21** (one encoder, channel A only, on the motor shaft) counted by PCNT; direction from the commanded direction. **Wheel 60 mm** (MCTR 1, 29 Oct). **Scale: 1050 counts per wheel turn** (ICD Table 15: 700 counts per motor turn, 1.5:1) → 0.180 mm per count. MCTR 1 wrote on 29 Oct that the wheel turns 1.5 times per motor turn, which would be ≈ 467 counts per wheel turn (0.404 mm per count). **The ICD value stays the default until the C1 test counts 10 wheel turns** and settles it (ICD Rev B note if it changes). `trolley_pos` is int16 mm, so it covers up to 32.7 m | ICD Tables 12–15, MCTR 1 note; MCTR 1 29 Oct | gear ratio direction |
| D12 | **Hoist (IF6.0 + IF15.0).** SPWM 20 kHz carrier on **J7.8 / J7.7 / J7.6 (phase A / B / C = GPIO38 / 39 / 40)**, sine frequency ∝ \|`hoist_cmd`\| up to 50 Hz, direction by swapping two phases. **DRV_EN J3.4** HIGH only while hoisting (**HIGH = enable** confirmed by EEII 1: IGBT on the DC bus with a driver and opto). **DRV_FAULT J3.5** → fault b6. **Overcurrent: 10 A stops the drive** (fault b0, EEII 1). **Brake J4.8**: HIGH = release, released 50 ms before the drive starts, clamped as soon as the speed reaches 0, on any fault and on lost link. `hoist_pos` / `hoist_vel` are **estimated** from the drive frequency and spool geometry | ICD Tables 19–20, 37–39, Table 34 | — |
| D13 | **Magnet (IF12.0).** PWM on **J7.5 = GPIO41, 20 kHz, 8.33 % duty** while `magnet_state` = 1; **MAG_LOGIC J3.6** HIGH while energised. `magnet_status` from J6.8: 1 = holding (current ≥ threshold within 2 s), 2 = fault. Only an explicit `magnet_state` = 0 turns it off; lost link and soft stop never do | ICD Tables 26–27, 32, 34; general requirement 2 (2 s) | — |
| D14 | **Overload.** Load > 20.0 kg (or ADC over-range) → fault b1 and **no hoisting up**. Lowering is still allowed | ICD Tables 35, 37 | — |
| D15 | **Soft stop** (Table 33 b0) is a software stop only. **The red hardware E-stop (PHY2) cuts HV by itself.** ICD Rev A gives the MCU no E-stop input, so `system_health` b1 stays 0. *Proposal for ICD Rev B:* E-stop auxiliary NC contact into J3.7 (GPA4, free) | ICD 3.5.6, SysRS PHY2, wiring doc J3.7 | dashboard-E-stop vs PHY2 wording |
| D16 | `error_code` = 1 + index of the lowest active `fault_flags` bit: 1 VFD overcurrent, 2 overload, 3 brake, 4 rail limit, 5 magnet, 6 encoder, 7 VFD fault, 8 wireless timeout. 0 = OK | ICD Table 34 (code + bitmask) | error-code list |
| D17 | **Anti-sway.** Dashboard-side jerk-limited S-curve on every speed command (baseline), plus a ZV shaper tuned from the pendulum period T = 2π√(L/g), with L taken from the hook height. Commands stay self-ending | ICD 3.5.6, SoW 4, D-04 | D-04 "proposed" |
| D18 | **Obstacle avoidance.** Planned on the dashboard from an operator-entered obstacle list (open-loop, planned navigation): raise to clearance → traverse → lower, sent as successive segments with `segment_idx` and the **Auto** flag (b2). Abort = soft stop | ICD Table 33 b2 + byte 7, SoW 5 | path-planning approach |
| D19 | Group channel plan **[4, 50, 73, 77, 81]**; Group 13 uses 77. The dashboard may switch to the clean channels 75–81 / 2–4 (Increment 2 surveys), over the crane's USB console only. An over-the-air switch needs a Table 33 change (ICD Rev B) and is deferred | ICD 3.5.2, Table 32 | rail survey D-07 |

**Not REII's to decide** (noted only): SysRS DR5 N8R2 vs the 16 MB flash fitted, the PT3 threshold, the IO-board owner. They don't affect REII's work.

---

# Ground station

NodeMCU-32S + nRF24L01+ on the operator PC's USB (COM10, 115 200 bit/s). It is the transmitter (PTX). It sends each 32-byte Table 33 command to `REII2`, and the crane's automatic ACK brings the 32-byte Table 34 telemetry back.

### Done

* **v4.1.0** (`groundstation_PIO/`). Boots straight into Normal Crane Control Mode (ch 77 / 2 Mbps / PA LOW / ICD profile).
* Wiring (ICD Table 31): CE GPIO4, CSN GPIO5, SCK GPIO18, MISO GPIO19, MOSI GPIO23, 3V3, GND. 10 µF + 100 nF at the module.
* Commands:

  | Purpose | Commands |
  |---|---|
  | USB check mode | `ECHO` |
  | Modes and profiles | `APP` / `CTRL`, `PROFILE ICD\|STATIC` |
  | Radio settings | `PA`, `CH` |
  | Test packets | `PING [seq]` → `PONG,…` + `TLM,…` |
  | Dashboard bridge | `CMD <64 hex>` → `ACK,<ok>,payload=<hex>,us=,arc=,seq=` |
  | Counters | `STATS`, `CLEAR` |
  | RF diagnostics | `SCAN`, `RPD`, `CARRIER` |

* Second build target `crane_rf24_check`, for diagnosis only: runs this RF24 code on the crane board, to split "crane hardware" from "crane firmware" faults.

### Flash

Open `Increment 4\groundstation_PIO` in VS Code with PlatformIO, then **Upload → Monitor**. Expected:

```
# REII groundstation v4.1.0 board=NodeMCU-32S app_ch=77 ctrl_ch=80 (Increment 4)
radio.begin() OK, isPVariant=1
GS,LINK,mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=ICD,payload=dyn32+ackpl32,tx_to=REII2,radio=OK
READY
```

### Changes needed to complete REII

The firmware already meets everything the ICD asks of the ground station. What remains is fit-out and evidence:

| # | Change | Why | When |
|---|---|---|---|
| G1 | **Rail fit-out:** USB cable ≤ 2 m, antenna vertical, module and capacitors fixed in a small enclosure or strain-relieved, ground station placed next to the PC | ICD Table 29 | before 5 Nov |
| G2 | **Rail RF evidence:** at the rail run `SCAN` (channel survey at the operator position), then `smoke_test.py --sweep` at 5–10 m with PA MAX, and again with the crane at the far end of the rail (up to ~30 m) | ICD Table 43 stage 2, D6 | 5 Nov |
| G3 | *Optional, only if time allows:* over-the-air channel change (new `msg_type 3`, both ends switch at an agreed sequence number and fall back to 77 after 1 s of silence) | D19; needs ICD Rev B | after the demo |
| G4 | Freeze and tag as **v5.0** with the crane and dashboard | Portfolio | 12 Nov |

---

# Crane MCU

ESP32-S3 main board (COM12, USB Serial/JTAG console). ICD-001 makes REII the owner of the **MCU side** of IF1.0 (MCTR 1), IF6.0 (EEII 1), IF12.0 (EEII 2), IF13.0 / IF15.0 (MCTR 2) and IF10.0. This firmware is therefore the meeting point of all five packages.

### Done (v4.4.1, `crane_rig_IDF/`)

* **ICD start-up sequence.** +3.3VP on (GPIO47), all SPI chip selects HIGH, SPI at 4 MHz on J1, MCP23017 out of reset, CE on J3.3 HIGH, load-cell tare.
* **Radio.** PRX on `REII2`, register read-back check (`cfg_errors=0`), 1 ms STATUS polling, Table 33 decode with sequence / duplicate check, Table 34 ACK payload re-loaded after every frame.
* **Load cell on J6** (real; see below). Overload fault b1 and lift inhibit.
* **Telemetry model** (`TLM MODEL`, the boot default) for every other field. Commands move a simulated trolley and hoist so the dashboard can be verified end to end. **These values are not measured.**
* **Console.** `HELP`, `INFO`, `STATS`, `PROFILE`, `MODE`, `PA`, `CH`, `SCAN`, `RPD`, `CARRIER`, `CE`, `REINIT`, `SELFTEST`, `TLM MODEL|ZERO`, `LOAD …`. The **BOOT** button runs `SELFTEST`.

### Pin map the firmware must drive (ICD + wiring doc)

| Package | Signal | Header | MCU resource | ICD |
|---|---|---|---|---|
| REII | nRF SPI / CSN / power | J1.1–J1.6, J1.8 | SPI2 (GPIO11/12/13), GPIO17, +3.3VP | Table 31 |
| REII | nRF CE | J3.3 | MCP23017 GPA0 | Table 31 |
| MCTR 1 | LAT_PWM_ENA | J7.9 | GPIO18 (LEDC) | Table 12 |
| MCTR 1 | MOT_IN1 / IN2 (IN3 / IN4 = copy of IN1 / IN2) | J4.4 / J4.5 (J4.6 / J4.7) | MCP GPB1 / GPB2 (GPB3 / GPB4) | Table 12 |
| MCTR 1 | ENC_A (ENC_B not used) | J7.12 (J7.11 free) | GPIO21 (GPIO4), PCNT | Table 13 |

**Encoder input level (MCTR 1, 29 Oct).** The ESP32-S3 inputs are **3.3 V only; none are 5 V tolerant** (absolute max ≈ 3.6 V), and a HIGH must be above 0.75 × 3.3 V ≈ 2.5 V. A level-shifter module is not needed:
* **Push-pull 5 V output:** voltage divider, **2.2 kΩ in series and 3.9 kΩ to GND** → 5 V × 3.9 / 6.1 = **3.20 V** HIGH. The divider's source resistance is 2.2 ∥ 3.9 ≈ 1.4 kΩ; with ≈ 10 pF on the pin that is ≈ 14 ns, far faster than the encoder pulses.
* **Open-collector output (most Hall encoders):** no divider. Pull the signal up to **3.3 V** with 4.7 kΩ (J6.12 +3.3VP) instead of 5 V, so it can never exceed 3.3 V. A divider on an encoder that has its own 10 kΩ pull-up to 5 V would only give ≈ 1.2 V HIGH and would not count.
* The HW-095 inputs accept the 3.3 V MCU outputs directly (ICD IF1.0).
| MCTR 2 | Load cell EXC± / SIG± | J6.1 / J6.2 / J6.5 / J6.6 | ADS1220 AIN0–AIN1 | Table 38 |
| MCTR 2 | HOIST_BRAKE | J4.8 | MCP GPB5 | Table 38 |
| EEII 1 | VFD_SPWM_A / B / C | J7.8 / J7.7 / J7.6 | GPIO38 / 39 / 40 (MCPWM, `spwm.c`) | Table 19 |
| EEII 1 | DRV_EN_LOGIC / DRV_FAULT_IN | J3.4 / J3.5 | MCP GPA1 / GPA2 (input) | Table 19 |
| EEII 1 | VFD current | digital input, pin TBD (J6.7 not used) | GPIO / MCP input, TBD | Table 19 → ICD Rev B (EEII 1 29 Oct) |
| EEII 2 | MAG_PWM | J7.5 | GPIO41 (LEDC) | Table 26 |
| EEII 2 | MAG_LOGIC | J3.6 | MCP GPA3 | Table 26 |
| EEII 2 | MAG_CURR_IN | J6.8 | ADS1220 AIN3 | Table 26 |
| spare | — | J3.7–J3.10, J4.9–J4.10, J7.4, J7.10 | GPA4–7, GPB6–7, free GPIOs | wiring doc |

### Changes needed to complete REII

These turn the bench model into the real crane, one interface at a time. Each item ends with a smoke test and a short dashboard session (see *Testing*). As each one lands, its Table 34 fields switch from "modelled" to "measured".

| # | Change | Details | Done when | Date |
|---|---|---|---|---|
| C1 | **Lateral drive + encoder (IF1.0)** | D11. `lateral.c`: LEDC timer 5 kHz / 10-bit on GPIO18; IN1–IN4 through the MCP port-B shadow register (IN3 / IN4 = IN1 / IN2); both IN low (coast) at 0 %. PCNT on GPIO21; mm = counts × π × 60 / `RIG_LAT_COUNTS_PER_WHEEL` (1050 default); `trolley_vel` over a 100 ms window; rail limits from position vs `RIG_MODEL_RAIL_MM` (fault b3); encoder fault b5 if ≥ 20 % is commanded for 0.5 s with no counts | Driver turns both ways from the dashboard; **count 10 wheel turns by hand: ≈ 10 500 (ICD) or ≈ 4 670 (MCTR 1 message)**, set the constant to the measured value; `trolley_pos` matches a tape | 30 Oct (LEDs / scope); MCTR 1 driver and motors after their own motor test on Mon 2 Nov |
| C2 | **Hoist brake (IF15.0)** | D12. Brake output on GPB5: LOW at boot. Release (HIGH) 50 ms before the drive starts; clamp at zero speed, any fault, soft stop, lost link, or when Hold Brake (b4) is set. `system_health` b3 = clamped | Brake module / LED follows the hoist slider with the 50 ms lead | 31 Oct |
| C3 | **Magnet (IF12.0)** | D13. `magnet.c`: LEDC 20 kHz on GPIO41 at 8.33 %, MAG_LOGIC GPA3. `magnet_status` and fault b4 from the AIN3 current; the threshold comes from EEII 2 | 8.33 % / 20 kHz measured on J7.5; status follows the coil current | 31 Oct |
| C4 | **Shared ADC (D10)** | `adc_sched.c` owns the ADS1220: 90 SPS, sequence L, L, M, gain 128 for the load cell and PGA bypass for the magnet current, AVDD reference. `loadcell.c` takes its samples from the scheduler. Magnet current in A × 10 with the EEII 2 scale (`rig_config.h`). VFD current comes from EEII 1's digital signal instead (C5) | `LOAD WATCH` noise no worse than now; magnet current reads 0.0 A with nothing connected | 1 Nov |
| C5 | **Hoist drive (IF6.0)** | D12. Enable the starter `spwm.c` (MCPWM, 20 kHz) on GPIO38 / 39 / 40. Sine frequency = \|`hoist_cmd`\| % × 50 Hz with the EEII 1 start frequency / V/f limits; up / down by swapping B and C; DRV_EN GPA1 HIGH (= enable) only while running; DRV_FAULT GPA2 → fault b6 + stop. VFD current from EEII 1's digital signal (format TBD); **overcurrent b0 at 10 A**, drive stopped. Hoist position / speed estimated from frequency × motor slip-free speed / gear ratio × spool circumference (MCTR 2 values) | Scope shows A / B / C 120° apart and the phase swap **before** any IGBT is powered; DRV_FAULT trips the drive. The EEII 1 board is soldered and tested 9–13 Nov with a 50 % duty signal generator, so the MCU SPWM is first verified on the scope only and connected to the VFD once EEII 1 signs off | 2 Nov (scope); with the VFD 12–13 Nov |
| C6 | **Safety state machine** | One owner for all outputs: `IDLE → RUN → SAFE_HOLD` (lost link, D8) / `FAULT` (VFD fault, overcurrent) / `SOFT_STOP` (b0). Outputs are always recomputed from state + latest command, never left half-set. Overload blocks hoist-up only (D14). Magnet untouched by every state except an explicit off (D13). Reset (b1) clears latched faults; Zero (b3) zeroes positions | Unplug the GS mid-move → motion stops ≤ 500 ms, brake clamps, magnet stays on; log line `RIG,SAFE_HOLD` | 1 Nov |
| C7 | **Real telemetry** | `rig_tlm_build()` takes each field from its driver once C1–C5 land. New mode `TLM REAL` becomes the boot default for the demo; `TLM MODEL` stays for bench work; `INFO` / heartbeat list the source of each field so the dashboard can badge it (B2) | Demo boots in `REAL`; heartbeat shows `src=` per field | 3 Nov |
| C8 | **Pin test for Table 43 stage 4** | Console `PINTEST`, run **with HV off**: steps each output (J7.9 PWM 25 %, J4.4–J4.7, J4.8, J7.5, J3.6, J3.4, the three SPWM phases at 1 Hz) for 2 s each and prints what it set. Also prints every input (J3.5, encoder counts, AIN2 / AIN3). Version → **v5.0** | `handshake.csv` with a ✔ per pin, signed off by each package owner | 4 Nov |

**Values each package must give REII** (placeholders in `rig_config.h` until then):
* MCTR 1: exact rail length (measure it at rail session 1); encoder output type (push-pull or open collector) so the right input circuit is fitted; their motor test result (Mon 2 Nov). *(Received 29 Oct: 60 mm wheel; encoder on the motor shaft; one HW-095 driving both motors; rail ≈ 20–30 m; rail session Wed 4 or Thu 5 Nov. Gear-ratio direction is settled by the C1 count.)*
* MCTR 2: spool radius, gear ratio, brake module wired to J4.8. *(Load cell: 2.002 mV/V, already wired to J6 — received 29 Oct.)*
* EEII 1: DRV_FAULT polarity, the current sensor's digital signal (pin, format, scale), start frequency, V/f limits, dead time. *(Received 29 Oct: DRV_EN HIGH = enable; 10 A overcurrent stops the drive; board ready 9–13 Nov.)*
* EEII 2: current-sensor V/A on J6.8, holding-current threshold.

### Flash

1. VS Code → **File → Open Folder** → `Increment 4\crane_rig_IDF` (open this folder on its own).
2. ESP-IDF status bar: target **esp32s3**, port **COM12**.
3. After any `sdkconfig.defaults` change, do a **full clean**, then **Build → Flash → Monitor**. From a terminal:
   * `idf.py fullclean`
   * `idf.py set-target esp32s3`
   * `idf.py -p COM12 build flash monitor`

Expected boot (abridged):

```
I (...) main: REII crane rig (Increment 4) v4.4.1 on esp32s3
I (...) main: MCP23017 at 0x20 responding with sane register defaults
I (...) loadcell: load cell on J6 (AIN0-AIN1, gain 128, 20 SPS, 50 Hz reject) ... taring now - keep the hook unloaded
I (...) rf_link: probe OK: SPI read-back good, SETUP_AW=0x03, nRF24L01+
RIG,BOOT,profile=ICD,mode=APP,ch=77,rate=2M,pa=LOW(-12dBm),...,ce_paths=J3.3(MCP GPA0),ce_j7_10=-1,ce_j3_3=1,cfg_errors=0
I (...) rf_link: link up: PRX on 'REII2' ch77/2M dyn32+ACK-payload telemetry (ICD), listening
RIG,LOAD_TARE,done
```

✅ `probe OK`, `cfg_errors=0`, `ce_j3_3=1`, no `RIG,ERR` lines. `nRF24 NOT RESPONDING` means a J1 SPI or power fault, not an RF fault.

### Load cell (IF13.0) — wiring and calibration

| Load cell wire (usual colour) | J6 pin | Goes to |
|---|---|---|
| EXC+ (red) | J6.1 | +3.3VP excitation and ADC reference |
| EXC− (black) | J6.2 | GND |
| SIG+ (green) | J6.5 | ADS1220 AIN0 |
| SIG− (white) | J6.6 | ADS1220 AIN1 |

Colours vary; follow the cell's datasheet. A shield goes to GND at the crane end only.

* kg = (reading − tare) × scale, sent as **kg × 10** in Table 34 bytes 10–11.
* Tare is automatic at boot, so boot with the hook empty.
* `LOAD CAL <kg>` with a known mass sets the scale exactly and is saved in flash.
* A crossed or open signal wire shows `saturated=1`. It is sent as 0xFFFF and treated as an overload.

| Command | What it does |
|---|---|
| `LOAD` / `LOAD WATCH [s]` | One reading, or 5 Hz for s seconds: raw, mV/V, kg, tare, scale |
| `LOAD TARE` | Re-zero with the hook empty (1 s average) |
| `LOAD CAL <kg>` | Calibrate with a known mass hanging; saved in flash |
| `LOAD RESET` | Back to the `rig_config.h` label scale (20 kg, 2.0 mV/V) |

Tare and calibrate can also be done from the dashboard (`--rig-port COM12`).

---

# Dashboard

The operator's PC application. A Python backend (`server.py`) owns the ground-station port and runs the 20 Hz command loop; a browser page at `http://localhost:5000` shows the crane and takes the controls. Build history, features, setup and usage are in **[`dashboard/README.md`](dashboard/README.md)**.

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\dashboard"
python -m pip install -r requirements.txt
python server.py --port COM10 --rig-port COM12 --open
```

### Changes needed to complete REII

| # | Change | Why | Date |
|---|---|---|---|
| B1 | **Fix the deadman flapping in background tabs.** The page sends its heartbeat with `setInterval(…, 200)`. Browsers throttle that to once a minute in a hidden tab, so the session of 28 Oct logged *Deadman … ramped to 0* / *page is back* every 60 s from 22:25 to 22:32. Fix: send the heartbeat from a Web Worker and tie liveness to the `/events` stream. When the tab is hidden, stop motion on purpose and require a click to resume (no silent re-enable) | OQ3 usability; a deadman that flaps can't be trusted | 3 Nov |
| B2 | **Show where each value comes from:** "measured" / "modelled" / "estimated" badges per readout, taken from the crane's `INFO` (C7). Hoist position labelled *estimated from drive frequency* | Avoids misreading bench-model values at the demo | 3 Nov |
| B3 | **PA MAX for the rail in the UI** (now command-line only), behind a confirm, with a reminder that it is for 5–10 m | D6, rail test | 3 Nov |
| B4 | **Auto mode + obstacle planner** (D18). Panel with an obstacle list (x from–to, height), a target position and height, and a plan preview drawn on the gantry. **Execute** sets Auto (b2) and steps `segment_idx`: up to clearance → across → down. Each segment ends on telemetry position (trolley encoder, hoist estimate) or a time-out. **Abort** = soft stop | SoW 5, FR13 | 3 Nov |
| B5 | **Anti-sway tuning** (D17). Ramp mode Off / S-curve / S-curve + ZV. ZV uses T = 2π√(L/g), with L = rope length from the hook height. Settings saved per session; sway test runs logged with the mode used | SoW 4, sway < 5° | 7 Nov |
| B6 | **Session summary for the report.** On Ctrl+C write `summary.json` (commands, PER, RTT min / avg / max, faults raised / cleared, time in each state) next to `telemetry.csv` | Verification chapter | 10 Nov |

---

# Testing and evidence

### Smoke test (bench and rail)

Close both serial monitors first; one program per COM port.

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\tools"
python -m pip install -r requirements.txt
python smoke_test.py --gs-port COM10 --rig-port COM12                 # ICD profile, 200 PINGs, PA LOW
python smoke_test.py --gs-port COM10 --rig-port COM12 --sweep         # PA MIN/LOW/HIGH/MAX (rail)
python smoke_test.py --gs-port COM10 --rig-port COM12 --profile both  # STATIC then ICD (diagnosis)
python smoke_test.py --gs-port COM10 --rig-port COM12 --diag-only     # "which radio is deaf / mute?"
```

Gates:
* **G1:** crane radio registers and CE.
* **G2:** ground-station radio.
* **G3:** both ends on the same profile / mode / PA.
* **G4:** 200 PINGs at 20 Hz. Pass = **PER < 5.0 %** and **max RTT < 4.15 ms** (ICD Table 32).

If there is no link at all, the RF diagnosis runs by itself: scans on both radios and carrier tests in both directions.

Output goes to `results/i4_bench/<run>/`: `smoke.csv` (per round, with `rig_rx` to separate "never arrived" from "ACK lost"), `sweep.csv`, `SUMMARY.md`, `summary.json`, both logs, `smoke.png`.

### Cross-check: crane hardware or crane firmware?

1. Flash the ground-station code's `env:crane_rf24_check` target onto the crane, with CE moved to J7.10 for this check only.
2. If the radio then hears Wi-Fi (`SCAN`) and PINGs are ACKed, the fault is in `crane_rig_IDF`.
3. If it is still deaf, the fault is in the board or harness. On 28 Oct the crane radio was deaf while the module worked on the NodeMCU. The cause was the harness: the **MISO** lead had been removed instead of the unused IRQ lead. A full rewire to the wiring document fixed it.
4. Re-flash `crane_rig_IDF` afterwards.

### Remaining verification (ICD 4.1 / 4.2, Table 43)

| Test | Pass criterion | Evidence | Date |
|---|---|---|---|
| Stage 1: PC ↔ GS USB | `ECHO` / `INFO` OK, dashboard runs on simulated values | dashboard session | ✔ 28 Oct |
| Stage 2: air link, bench | < 5 % PER, < 4.15 ms | `results/i4_bench/20261028_230328` | ✔ 28 Oct |
| Stage 2: air link, rail 5–10 m, PA MAX, drives running | same | smoke `--sweep` on the rail | 5 Nov |
| Stage 3: J1 + J3.3 | `probe OK`, `ce_j3_3=1` | G1 gate | ✔ 28 Oct |
| Stage 4: full-group signal handshake, HV off | every pin follows `PINTEST` / dashboard | `handshake.csv` (C8) | 4 Nov |
| Load cell vs known masses | within 0.1 kg | dashboard CSV + photo | 6 Nov |
| Encoder position vs tape | error recorded | dashboard CSV | 5 Nov |
| Lost link mid-move | stop ≤ 500 ms, brake clamps, magnet stays on | video + `events.log` | 9 Nov |
| Hardware E-stop | HV removed independent of software (PHY2) | video | 9 Nov |
| Overload > 20 kg | hoist-up inhibited, fault b1 | dashboard CSV | 6 Nov |
| Anti-sway flat vs shaped | sway reduced, < 5° target | video + CSV | 7 Nov |
| Obstacle-avoidance path | clears the obstacle, ends at the target | video + CSV | 9 Nov |

### Demo checklist (13 Nov)

1. Before power-on: antennas fitted, 10 µF + 100 nF at both radios, J1.8 = 3.3 V.
2. Crane boots: `cfg_errors=0`, `ce_j3_3=1`, tare done with the hook empty.
3. Dashboard started with `--rig-port`. Link OK, PA MAX on the rail, telemetry badges show "measured".
4. Show each package from the dashboard: lateral move with position → hoist up / down with brake → magnet on, current shown → load reading.
5. Show the anti-sway ramp, then an auto obstacle path.
6. Show the safety behaviour: soft stop, lost link (unplug the GS USB), hardware E-stop.
7. Save the session folder (`results/dashboard/<session>`) for the portfolio.

---

## Rules for the remaining work

* **One change at a time.** Re-run the smoke test and a short dashboard session after each, and keep the result folder. If PER rises, the last change is the cause.
* All pins and limits live in `rig_config.h` (crane), `gs_config.h` (ground station) and `dashboard/radio.py` / command-line options. Don't hard-code them anywhere else.
* Anything that changes a pin, a byte in Table 33 / 34, or a timing in the ICD goes to the group as an **ICD Rev B** change first (E-stop input D15, OTA channel D19).
