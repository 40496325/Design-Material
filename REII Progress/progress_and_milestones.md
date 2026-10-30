# REII Progress and Milestones — Group 13

**Work package:** REII – Remote Control Dashboard and RF Ground Station
**Owner:** R Nel (40496325)
**Last updated:** Thursday 29 October 2026
**Replaces:** the 15 Oct record (copy kept in `REII Progress/archive/`). That record was written before the ICD was signed and was built around open questions. Those are now closed by the decisions in `Increment 4/README.md` → *Locked decisions*.

Authority used for every decision and plan in this file:

1. **ICD-001 Rev A** (Group 13 report, 23/24 Oct 2026): `Increment 3/Interface Control Documentation/(ICD v1) Engineering Design 2026 Group 13 Report.pdf`
2. **MCU wiring and header pin allocation**: `NEWEST main control board firmware/MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx`
3. **SysRS v1.0** and the **REII SoW**: `Eng Package Requirements/`
4. **Lecture slides** for dates: `Lecture Slides/7.1 EngDes26 - Checkpoint Breakdown.pptx` (key dates) and `0 EngDes26 - Intro (3).pptx` (increments)

---

## 1. Where REII stands (29 Oct)

| Part | State | Evidence |
|---|---|---|
| RF link, crane ↔ ground station (IF10.0) | **Verified on the bench** with CE on J3.3 as the ICD specifies: 200/200 PINGs, 0.0 % PER, max RTT 0.84 ms, 200/200 ACKs carrying a valid Table 34 frame | `Increment 4/results/i4_bench/20261028_230328/` |
| 32-byte protocol (ICD Tables 33 / 34) | **Implemented on all three ends**: crane, ground station and dashboard | `crane_rig_IDF/main/icd_frames.h`, `groundstation_PIO/include/icd_frames.h`, `dashboard/icd.py` |
| Ground station (NodeMCU-32S) | **Done**, firmware v4.1.0 | `Increment 4/groundstation_PIO/` |
| Dashboard (FR12 / FR13) | **Working**, 20 Hz control loop, live telemetry, faults, S-curve ramps, deadman, CSV capture, PA / channel switching | `Increment 4/dashboard/`, sessions in `Increment 4/results/dashboard/` |
| Load cell (IF13.0) | **Real readings on the dashboard**: ADS1220 on J6, tare and calibration, overload fault | `crane_rig_IDF/main/loadcell.c`, dashboard sessions 28–29 Oct |
| Crane MCU outputs and sensors for MCTR 1, MCTR 2 brake, EEII 1, EEII 2 (MCU side is REII's per the ICD) | **Not started.** Commands only feed a bench model; positions and currents are modelled | `crane_rig_IDF/main/rig_telemetry.c` (`TLM MODEL`) |
| Anti-sway (SoW item 4) | **Baseline done** (jerk-limited S-curve on the dashboard); model, shaper and rail validation still to do | `dashboard/ramp.py` |
| Obstacle avoidance (SoW item 5) | **Not started** | — |
| Rail integration and validation (SoW item 6) | **Not started** | — |

In short, the link, protocol, ground station, dashboard and load cell work. What remains to finish the module is:
- the crane-side drive and sense code for the other four packages;
- the auto/obstacle mode;
- the anti-sway tuning;
- final integration, rail testing, the demo and the individual report, **all due Fri 13 Nov**;
- then the group presentation and portfolio on 16 Nov.

---

## 2. Key dates

| Date | Event | Source |
|---|---|---|
| Fri 11 Sep | Individual checkpoint (I1 + I2) — **submitted** | slide 7.1 |
| Mon 14 Sep | Increment 3 begins, ICD drafted | slide 7.1 |
| Fri 23 Oct | ICD-001 Rev A / Group 13 report — **submitted** | ICD Table 2 |
| Tue 27 Oct | Participation marks captured | slide 7.1 |
| Wed 4 Nov, Wed 11 Nov | Lectures (Wednesdays 14:30) | slide 0, "In practice" |
| **Fri 13 Nov** | **Final integration + demonstration of the working crane system + individual report** (format of the 2025 example `2025 Reporting Example/REII327_Final_Report_41942442.pdf`) | slide 7.1 (demo); report date confirmed by the group |
| **Mon 16 Nov** | **Final Group Presentation and Individual Portfolio** | slide 7.1 |

That leaves **15 days** from today to the demo. The plan in §5 therefore runs crane interface work and dashboard work in parallel this week, does rail work next week, and keeps the last three days before the demo for verification and the report.

---

## 3. Milestones

Status values: **DONE**, **VERIFIED** (done, with measured evidence), **IN PROGRESS**, **PLANNED**.

| # | Milestone | Traces to | Status | Evidence / remaining work | Target |
|---|---|---|---|---|---|
| M1 | Requirements baseline + RF characterisation report | SysRS FR12–FR15, SoW 1 | DONE | `Increment 2/REII_nRF24_Link_Characterisation/` (report submitted 11 Sep) | 11 Sep ✔ |
| M2 | ICD-001 Rev A: IF10.0 complete, MCU side of IF1.0 / IF6.0 / IF12.0 / IF13.0 / IF15.0 | SoW 2 | DONE | ICD Tables 28–39; signed 23/24 Oct | 23 Oct ✔ |
| M3 | Ground station NodeMCU-32S, USB-powered | SoW 1, ICD 3.5.1 | DONE | `groundstation_PIO` v4.1.0, G2 gate PASS | ✔ |
| M4 | Crane radio on J1 + CE J3.3, bench air link | SoW 1, ICD Table 31, Table 43 stages 2–3 | VERIFIED | Run `20261028_230328`: 0.0 % PER, RTT max 0.84 ms (limit 4.15) | ✔ |
| M5 | 32-byte protocol on all ends, ACK-payload telemetry | SoW 2, ICD Tables 33 / 34 | VERIFIED | 200/200 valid 0x81 frames in the same run; dashboard decodes every field | ✔ |
| M6 | Dashboard core: monitor + control | SoW 3, FR12, FR13, OQ3 | DONE (bench) | `dashboard/` v4.x; remaining items in README → *Dashboard* | 3 Nov |
| M7 | Load cell IF13.0: ADS1220, tare, calibration, overload | ICD Tables 37–39, FR5 | DONE (bench) | Calibrate with known masses on the real hoist with MCTR 2 | 6 Nov |
| M8 | Crane outputs: lateral PWM / dir, brake, magnet PWM / logic, SPWM / enable | ICD IF1.0, IF15.0, IF12.0, IF6.0 | PLANNED | Crane firmware v5 (README → *Crane MCU* C1–C5) | 2 Nov |
| M9 | Crane sensors: encoder count, VFD + magnet current, VFD fault input | ICD Tables 13, 15, 19, 26 | PLANNED | ADC sharing (D10), PCNT encoder (D11) | 2 Nov |
| M10 | Safety state machine: lost-link safe hold, overload inhibit, soft stop, brake timing | FR6, FR14, ICD Table 32 + operating sequence, Table 39 | PLANNED | Replaces the telemetry-only flags | 1 Nov |
| M11 | Unpowered full-group signal handshake (Table 43 stage 4) | ICD 4.2.5 stage 4 | PLANNED | `PINTEST` on the crane, scope / LED on every pin, CSV evidence | 4 Nov |
| M12 | Obstacle avoidance: auto mode with planned segments | SoW 5, ICD Table 33 b2 + `segment_idx` | PLANNED | Dashboard planner + crane segment execution | 3 Nov |
| M13 | Anti-sway: pendulum model, ZV shaper, flat-vs-shaped sway test | SoW 4, ICD 3.5.6, general requirement 3 (sway < 5°) | IN PROGRESS | S-curve done; measure sway period on the rail, add ZV | 7 Nov |
| M14 | Rail RF at full power 5–10 m, with drives running | ICD Table 29 / 43 stage 2, SoW 6 | PLANNED | `smoke_test.py --sweep` at PA MAX on the rail; `SCAN` survey at the rail | 5 Nov |
| M15 | Rail integration: each package driven from the dashboard, readings vs instruments | ICD 4.1 / 4.2, SoW 6 | PLANNED | MCTR 1 (5 Nov), MCTR 2 + EEII 2 (6 Nov), EEII 1 (VFD board soldered and tested 9–13 Nov; MCU SPWM connected once EEII 1 signs off) | 13 Nov |
| M16 | Acceptance tests: lost-link, E-stop, overload, PT1 / PT2 timing support | FR14, PHY2, ICD 4.2 | PLANNED | Test log + videos | 10 Nov |
| M17 | **Final demonstration** | — | PLANNED | Demo checklist (README) | **13 Nov** |
| M18 | **Individual report** (2025 example structure) | — | PLANNED | §6 below | **13 Nov** |
| M19 | **Group presentation + individual portfolio** | — | PLANNED | REII slides + portfolio pack | **16 Nov** |

---

## 4. Done since the last record (15 Oct → 29 Oct)

| Date | Work | Evidence |
|---|---|---|
| 16 Oct | 200-packet smoke test on the static-payload profile: 200/200 received by the crane, 192/200 ACKed, 0.89 ms mean RTT (CE on J7.10) | `Increment 3/results/i3_bench/20261016_125349/` |
| 18–22 Oct | Table 33 / 34 pack and unpack, first dashboard (Flask), S-curve ramp concept, ICD sections 2.5, 3.5, 4 and 5.5 written | `Increment 3/dashboard_Pycharm_firmware/`, ICD |
| 23–24 Oct | ICD-001 Rev A finalised and signed | ICD Tables 2–3 |
| 27–28 Oct | Fault-finding the "dashboard shows no ACKs / crane 0 frames" problem. Causes found:<br>(1) payload-mode mismatch: crane static 32 B vs ground station DPL + ACK payload;<br>(2) CE raised before PRIM_RX was set when CE moved to J3.3;<br>(3) crane radio deaf on the crane board even though its registers were correct. The module itself heard normally on the NodeMCU, which pointed to the harness. **Root cause:** when the unused IRQ lead was meant to be removed, the **MISO** lead was removed instead. **Fixed** by a full rewire of the radio harness following the MCU wiring and header pinout document | REII Progress log, `Increment 4/results/i4_bench/20261028_19*` (diagnosis.json: crane scan 0 hits) |
| 28 Oct | Increment 4 rebuilt from the starter project, Increment 2 and the ICD: crane `crane_rig_IDF` (v4.4.1), ground station `groundstation_PIO` (v4.1.0), smoke test tool with automatic RF diagnosis | `Increment 4/` |
| 28 Oct 21:25 → 23:03 | ICD-profile smoke tests PASS in every run. One STATIC-profile run failed only on RTT (4.64 ms, 1.0 % PER); STATIC is diagnostic only (D3). Last run with CE on **J3.3 only**: 0.0 % PER, 0.84 ms max RTT, telemetry 200/200 | `results/i4_bench/20261028_212514` … `_230328` |
| 28 Oct | Load cell on J6 via the ADS1220 (gain 128, ratiometric, 50 Hz rejection), tare at boot, `LOAD CAL`, overload > 20 kg fault, value sent in Table 34 bytes 10–11 | `crane_rig_IDF/main/loadcell.c`, `ads1220.c` |
| 28–29 Oct | Dashboard v4: live load with USB cross-check, Tare / Calibrate buttons, PA LOW / HIGH and clean-channel switching (77 → 80 → 77 verified), deadman, soft stop, event log, per-command CSV | `results/dashboard/20261028_220935/`, `20261029_000031/` |

> **Lesson for the report:** the full rewire between the last failing run (21:05) and the first passing run (21:25) on 28 Oct is what restored the link; the firmware CE setting was the same in both runs. Before any power-on, check every lead against the wiring document pin by pin, especially after a lead is added or removed (ICD 4.1.1, "Power & Wiring Check").

---

## 5. Plan to finish (29 Oct → 16 Nov)

Teammate inputs each day depends on are in §7. Section labels C1–C5, G1–G4 and B1–B6 refer to the change lists in `Increment 4/README.md`.

| Date | REII work | Done when | Needs |
|---|---|---|---|
| Thu 29 Oct | Refresh documentation: progress, timesheet, READMEs, decisions | Files updated | — |
| Fri 30 Oct | **Crane C1: lateral (IF1.0).** LEDC PWM on J7.9, IN1–IN4 on J4.4–J4.7, PCNT encoder on J7.12 → real `trolley_pos` / `trolley_vel` | Firmware on LEDs / scope first; then the MCTR 1 driver turns both ways from the dashboard; 10 wheel turns counted (ICD 1050 per turn vs ≈ 467 from MCTR 1's note) | MCTR 1: driver + motors after their own motor test (Mon 2 Nov); encoder input circuit (divider or 3.3 V pull-up) |
| Sat 31 Oct | **Crane C2 + C3: brake (IF15.0) and magnet (IF12.0).** J4.8 timing, J7.5 PWM 20 kHz at 8.33 %, J3.6 logic | Brake LED / module follows the hoist command; magnet PWM measured on a scope | MCTR 2 brake module; EEII 2 switch module |
| Sun 1 Nov | **Crane C4 + C6: ADC sharing (D10) and the safety state machine.** Load cell + AN3 (magnet current); lost-link safe hold, overload inhibit, soft stop | Unplug the GS → speeds 0, brake clamps, magnet stays on; log shows it | — |
| Mon 2 Nov | **Crane C5: hoist drive (IF6.0).** SPWM on J7.6–J7.8 (starter `spwm.c`), DRV_EN J3.4, DRV_FAULT J3.5, hoist position estimate | 3 phases 120° apart on a scope **before** any IGBT is powered | EEII 1: V/f limits, DRV_FAULT polarity (board itself only ready 9–13 Nov, so scope only); MCTR 2: spool radius, gear ratio |
| Tue 3 Nov | **Dashboard B1–B4.** Background-tab heartbeat fix, telemetry source badges, PA MAX in the UI, auto mode + obstacle planner | Auto run of a planned path in the bench model | — |
| Wed 4 Nov | **Integration stage 4 (unpowered)**, run with `PINTEST`; lecture | `handshake.csv` with every pin checked | All packages' connectors on the board |
| Thu 5 Nov | **Rail session 1 (MCTR 1).** PA MAX smoke test + sweep at 5–10 m and at the far end of the rail, `SCAN` at the rail, measure the rail length (MCTR 1 estimate 20–30 m), lateral motion, encoder vs tape | Smoke PASS at range; rail length and position error recorded | Rail slot (MCTR 1 proposes Wed 4 or Thu 5 Nov, if their Mon 2 Nov motor test passes), MCTR 1 carriage on the rail |
| Fri 6 Nov | **Rail session 2 (MCTR 2 + EEII 2).** Load cell with known masses, brake release/clamp, magnet on/off + current | 0.1 kg readings vs reference masses; brake holds | Hoist on the rail, known masses, magnet |
| Sat 7 Nov | **Anti-sway (M13).** Measure the pendulum period at 3 rope lengths, set the ZV shaper, record flat-vs-shaped sway video | Sway amplitude reduced, < 5° target | Payload on the hook |
| Sun 8 Nov | **Report:** chapters 1–2 (introduction, design process: SCD, architecture, FFBD, requirements) | Draft chapters | Figures from `Increment 3/Interface Control Documentation/System Architecture/` |
| Mon 9 Nov | **Rail session 3 (full system).** E-stop test, lost-link test mid-move, obstacle-avoidance run. EEII 1 starts final soldering and signal-generator tests (50 % duty) on their VFD board the same day | Test log + video per test | Hoist shown with the brake and SPWM on the scope; MCU → VFD hook-up once EEII 1 signs off (target 12–13 Nov) |
| Tue 10 Nov | **Verification:** requirement verification matrix, PT1 / PT2 timing from dashboard CSV; **report** chapter 3 (trade study, calculations, implementation evidence) | Matrix complete | Test data from 5–9 Nov |
| Wed 11 Nov | **Report** chapter 4 (verification); demo dry run with the group; lecture | Dry run passes the checklist | Whole group |
| Thu 12 Nov | **Report** chapters 5–7, final read-through; firmware freeze v5.0 (crane, GS, dashboard) | PDF ready | — |
| **Fri 13 Nov** | **Final demonstration; submit the individual report** | — | — |
| Sat 14 – Sun 15 Nov | REII slides for the group presentation; compile the portfolio (report, timesheet, test logs, CSVs, code) | Slides merged into the group deck | Group |
| **Mon 16 Nov** | **Final group presentation and individual portfolio** | — | — |

**Fallbacks.** If a teammate's hardware is late, the matching crane output is verified on the bench with an LED, a scope or a logic analyser, and the field stays on `TLM MODEL` with a visible "modelled" badge on the dashboard. The demo still shows the full command path. The report states which interfaces were verified on hardware and which on the bench.

---

## 6. Individual report (due 13 Nov) — structure and sources

The chapter layout follows the 2025 example, `REII327_Final_Report_41942442.pdf`.

| Chapter (2025 layout) | REII content | Sources already in the repo |
|---|---|---|
| 1 Introduction (purpose, role in the larger system, scope) | Remote control dashboard + RF ground station for the Group 13 gantry crane | SoW, SysRS 1.1, ICD 1.1.5 |
| 2.1 Context analysis (SCD) | Operator, PC dashboard, ground station, crane controller, other packages | `Increment 3/Interface Control Documentation/System Architecture/` |
| 2.2 System architecture (physical diagram, functional units, interfaces) | FU6 ↔ FU8, IF10.0, IF-E1.0, MCU side of IF1 / 6 / 12 / 13 / 15 | ICD Fig 1, Tables 5–6, 28–34 |
| 2.3 Functional and behavioural analysis (mission, FFBD L0 / L1) | Connect → command → telemetry → safe hold | `System_FFBD.drawio` |
| 2.4 Resource mapping | Functions → ground station / crane / dashboard | this file §3 |
| 2.5 Key requirements | FR12, FR13, FR14, OQ3, DR4; SoW 1–6; ICD limits (PER < 5 %, RTT < 4.15 ms, 20–50 Hz) | SysRS, ICD Table 32 |
| 3.1 Trade study | Ground station MCU (Nano vs NodeMCU-32S vs STM32), channel / rate, payload profile (static vs ACK payload), CE path (J7.10 vs J3.3), load-cell path (ADS1220 vs HX711), dashboard stack | Increment 2 report, ICD 3.5, README → *Locked decisions* |
| 3.2 Calculations | Air time and worst-case delivery (4.15 ms), 32-byte budgets, load-cell resolution (gain 128, mV/V), encoder counts → mm, pendulum period and ZV timing | ICD Tables 32–34, `loadcell.c`, `ramp.py` |
| 3.3 Implementation evidence (wiring, firmware screenshots) | Crane, ground station and dashboard screenshots, wiring photos | `Increment 4/`, dashboard sessions |
| 3.4 Alignment with requirements | Requirement → implementation table | README |
| 4 Verification (setup, results, matrix, evidence) | Smoke tests (bench + rail), dashboard sessions, safety tests, sway test | `results/i4_bench/`, `results/dashboard/` |
| 5–7 Conclusions, future work, references | Including the 28 Oct link fault analysis as lessons learned | this file §4 |

---

## 7. Inputs needed from the team (requested by REII)

| From | Needed | For | By |
|---|---|---|---|
| MCTR 1 (AR Cronje) | Encoder output type (push-pull or open collector) and the input circuit fitted; motor test result; exact rail length (measured 5 Nov) | C1, rail limits | 2 Nov |
| MCTR 2 (VA King) | Spool radius, gear ratio, brake module wired to J4.8, 2–3 known masses | C2, hoist position estimate, load-cell calibration | 31 Oct / 6 Nov |
| EEII 1 (MM Van Rooyen) | DRV_FAULT polarity; the current sensor's digital signal (which pin, what format, scale); start frequency, V/f limits, dead time | C5, `vfd_current` | 2 Nov |
| EEII 2 (F Prinsloo) | Switch module + level shifter, current-sensor V/A scale on J6.8, "holding" current threshold | C3, `magnet_current`, `magnet_status` | 31 Oct |
| Group | Rail slots on 5, 6 and 9 Nov; demo order; presentation template | M14–M17 | 3 Nov |

**Answers received (29 Oct)**

| From | Answer | Effect |
|---|---|---|
| MCTR 2 | Load cell output **2.002 mV/V**; cell already wired and connected to J6 | `RIG_LOADCELL_SENS_MV_V` set to 2.002 in `rig_config.h`; no wiring question left |
| EEII 1 | VFD board: **final soldering and testing 9–13 Nov**, tested with a **50 % duty signal generator** | C5 verified on the scope first; MCU → VFD hook-up only after EEII 1 signs off (target 12–13 Nov) |
| EEII 1 | **IGBT on the DC bus with a driver and opto: logic HIGH = enable** on DRV_EN (J3.4) | Confirms D12 |
| EEII 1 | Current sensor **has a DAC and needs a digital input** | Not read on J6.7 (AIN2); pin and signal format to agree → ICD Rev B; ADC sharing reduced to load cell + magnet current (D10) |
| EEII 1 | **10 A stops the drive** | Overcurrent fault b0 at 10 A |
| MCTR 1 | **60 mm wheel**; wheel turns 1.5 times per motor turn; **encoder on the motor shaft** | Wheel size used in C1. The ratio is the reverse of ICD Table 15 (1050 counts per wheel turn); the ICD value stays the default until the C1 test counts 10 wheel turns |
| MCTR 1 | Rail **≈ 20–30 m** (estimate) | Measure at rail session 1; RF test also at the far end; `trolley_pos` (int16 mm) covers up to 32.7 m; model rail set to 20 m |
| MCTR 1 | **One HW-095 drives both motors** from one set of MCU signals, as planned | ENB jumpered to ENA; IN3 / IN4 copy IN1 / IN2 (J4.6 / J4.7 or jumpered on the driver); one motor per driver channel |
| MCTR 1 | Asked whether the MCU has 5 V inputs | No: 3.3 V only. Push-pull encoder → 2.2 kΩ / 3.9 kΩ divider (3.2 V); open collector → 4.7 kΩ pull-up to 3.3 V, no divider (README, pin map) |
| MCTR 1 | Motor test with both motors by **Mon 2 Nov**; rail session **Wed 4 or Thu 5 Nov** | C1 hook-up after their test; rail session 1 stays on Thu 5 Nov |

---

## 8. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Teammate hardware late (VFD board confirmed only ready 9–13 Nov; magnet module) | MCU-driven hoisting may only be shown in the last days before the demo | SPWM verified on the scope on 2 Nov so hook-up is plug-in only; book a joint EEII 1 + REII session on 12 Nov; keep the hoist fields modelled and badged until then; report it honestly |
| Harness errors when packages connect their leads to the shared board (the 28 Oct link fault was a removed MISO lead) | A subsystem silently stops working | Pin-by-pin check against the wiring document before each power-on; `PINTEST` (C8) after every rewire; label every Dupont lead |
| EMI from the VFD and motors on the rail | PER rises | PA MAX at 5–10 m, antenna clear of the 220 V leads (ICD Table 29), `SCAN` survey on the rail, channel switch on the dashboard |
| ADC sharing lowers the load-cell rate or noise performance | Noisier load reading | D10 schedule keeps the load cell ≥ 20 Hz; verify noise with `LOAD WATCH`; fall back to 20 SPS if needed |
| Too little time for anti-sway and obstacle avoidance | SoW items 4–5 partly shown | Keep the S-curve as the guaranteed baseline; ZV and planner are self-contained dashboard features |
| Report squeezed by rail work | Weak report | Report chapters scheduled on 8, 10, 11 and 12 Nov, separate from rail days |
