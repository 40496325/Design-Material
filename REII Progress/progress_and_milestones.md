# REII Progress and Milestones — Group 13

**Work package:** REII – Remote Control Dashboard and RF Ground Station
**Owner:** R Nel (40496325)
**Last updated:** Tuesday 17 November 2026
**Replaces:** the 29 Oct record (copy kept in `REII Progress/archive/progress_and_milestones_2026-10-29.md`). The 29 Oct plan worked towards a demo and report on 13 Nov and a presentation on 16 Nov. **Those dates no longer apply.** The final dates are now:

| Event | Date |
|---|---|
| **Demonstrations** | **Wednesday 2 December 2026** |
| **Presentations** | **Thursday 3 December 2026** |
| **Final documentation** (individual report + portfolio), uploaded to **eFundi** | **Friday 4 December 2026, 08:00** |

Authority used for every decision and plan in this file:

1. **ICD-001 Rev A** (Group 13 report, 23/24 Oct 2026): `Increment 3/Interface Control Documentation/(ICD v1) Engineering Design 2026 Group 13 Report.pdf`
2. **MCU wiring and header pin allocation**: `NEWEST main control board firmware/MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx`, **the final pinout for every package's connection to the MCU. Only pins stated there are used** (rule set 17 Nov)
3. **SysRS v1.0** and the **REII SoW**: `Eng Package Requirements/`
4. **Final dates** (above): announced to the group, 17 Nov 2026. They replace the 13 / 16 Nov dates from `Lecture Slides/7.1 EngDes26 - Checkpoint Breakdown.pptx`

---

## 1. Where REII stands (17 Nov)

| Part | State | Evidence |
|---|---|---|
| RF link, crane ↔ ground station (IF10.0) | **Verified on the bench** with CE on J3.3 as the ICD specifies: 200/200 PINGs, 0.0 % PER, max RTT 0.84 ms, 200/200 ACKs carrying a valid Table 34 frame | `Increment 4/results/i4_bench/20261028_230328/` |
| 32-byte protocol (ICD Tables 33 / 34) | **Implemented on all three ends**: crane, ground station and dashboard | `crane_rig_IDF/main/icd_frames.h`, `groundstation_PIO/include/icd_frames.h`, `dashboard/icd.py` |
| Ground station (NodeMCU-32S) | **Done**, firmware v4.1.0 | `Increment 4/groundstation_PIO/` |
| Spare ground station (ESP32-C3) | **Firmware prepared 9 Nov** (v4.1.0-c3, same protocol, C3 pins). The README says it was not compile-checked when written; the build, smoke test and ICD Rev B note are still to do | `Increment 4/groundstation_C3/README.md` |
| Dashboard (FR12 / FR13) | **Working**: 20 Hz control loop, live telemetry, faults, S-curve ramps, deadman, CSV capture, PA / channel switching | `Increment 4/dashboard/`, sessions in `Increment 4/results/dashboard/` |
| Load cell (IF13.0) | **Real readings on the dashboard**: ADS1220 on J6, tare and calibration, overload fault | `crane_rig_IDF/main/loadcell.c`, dashboard sessions 28–29 Oct |
| Crane MCU outputs and sensors for MCTR 1, MCTR 2 brake, EEII 1, EEII 2 (MCU side is REII's per the ICD) | **EEII 1 drive + hoist brake (v4.5.0, 17 Nov)**: 10 kHz SPWM, DRV_EN, brake, DRV_FAULT input, dashboard up/down, `DRIVE` test commands; v4.5.0 **built and booted on the board 17 Nov** (10 kHz ISR confirmed). **MCTR 1 trolley (v4.6.0, 17 Nov)**: ENA PWM J7.9, IN1–IN4 J4.4–J4.7, encoder J7.12, 20 m rail limits, dashboard left/right, `TROLLEY` test commands; host-checked, board build next. **EEII 2 magnet (v4.7.0, 18 Nov)**: MAG_PWM J7.5 (GPIO41) 15 kHz ON 99 % / release 10 %, MAG_LOGIC J3.6, coil current J6.8 via the shared ADS1220 (C4 for the magnet), holding status + fault b4, `MAGNET` test commands; host-checked, board build next. **Final pass 18 Nov:** 12 V supply, 99 %, holding 7.5 A, coil ≈ 1 Ω → ≈ 11.4 A / ≈ 140 W expected; release-current (> 0.5 A) and over-current (> 10 A) warnings added; later changes only in `main.c`. To check on the rig: CAL at full current, the 10 % release on 12 V, coil heating. VFD current J6.7 is still not read | `crane_rig_IDF/main/drive_electronics/`, `crane_rig_IDF/main/trolley/`, `crane_rig_IDF/main/magnet/` |
| Anti-sway (SoW item 4) | **Baseline done** (jerk-limited S-curve on the dashboard); model, shaper and rail validation still to do | `dashboard/ramp.py` |
| Obstacle avoidance (SoW item 5) | **Not started** | — |
| Rail integration and validation (SoW item 6) | **Not started** | — |

In short, the link, protocol, ground station, dashboard and load cell work. The 30 Oct – 16 Nov plan was not carried out, so all of that work moves into the new window (§5). What remains:
- the crane-side drive and sense code for the other four packages (C1–C8);
- the auto/obstacle mode and the anti-sway tuning;
- rail integration and acceptance testing;
- the **demonstration on Wed 2 Dec**, the **presentation on Thu 3 Dec** and the **final documentation on eFundi by Fri 4 Dec 08:00**.

**New request (17 Nov): EEII 1 wants a 10 kHz SPWM carrier.** ICD Rev A (Tables 16, 18, 19, 20) and decision D12 say 20 kHz. Firmware impact is one constant (`SPWM_CARRIER_HZ` in `spwm.c`). The duty table and phase step scale from it automatically. This is logged as an **ICD Rev B change** and will be made as part of C5 once EEII 1 confirms the questions in §7.

---

## 2. Key dates

| Date | Event | Source |
|---|---|---|
| Fri 11 Sep | Individual checkpoint (I1 + I2) — **submitted** | slide 7.1 |
| Mon 14 Sep | Increment 3 begins, ICD drafted | slide 7.1 |
| Fri 23 Oct | ICD-001 Rev A / Group 13 report — **submitted** | ICD Table 2 |
| Tue 27 Oct | Participation marks captured | slide 7.1 |
| ~~Fri 13 Nov~~ | ~~Final integration + demo + individual report~~ — **superseded** | slide 7.1 (old) |
| ~~Mon 16 Nov~~ | ~~Group presentation and individual portfolio~~ — **superseded** | slide 7.1 (old) |
| **Wed 2 Dec** | **Demonstrations** (working crane system) | group, 17 Nov |
| **Thu 3 Dec** | **Presentations** (group presentation) | group, 17 Nov |
| **Fri 4 Dec, 08:00** | **Final documentation on eFundi**: individual report (format of the 2025 example `2025 Reporting Example/REII327_Final_Report_41942442.pdf`) and portfolio | group, 17 Nov |

From today that leaves **15 days to the demonstration** and **17 days to the final documentation**. The 08:00 deadline means the documentation has to be finished on **Thu 3 Dec**, after the presentation. The plan in §5 runs the crane-firmware work first (17–23 Nov), rail work next (24–27 Nov), and keeps 28 Nov – 1 Dec for verification, the freeze, the dry run and the report.

---

## 3. Milestones

Status values: **DONE**, **VERIFIED** (done, with measured evidence), **IN PROGRESS**, **PLANNED**.

| # | Milestone | Traces to | Status | Evidence / remaining work | Target |
|---|---|---|---|---|---|
| M1 | Requirements baseline + RF characterisation report | SysRS FR12–FR15, SoW 1 | DONE | `Increment 2/REII_nRF24_Link_Characterisation/` (report submitted 11 Sep) | 11 Sep ✔ |
| M2 | ICD-001 Rev A: IF10.0 complete, MCU side of IF1.0 / IF6.0 / IF12.0 / IF13.0 / IF15.0 | SoW 2 | DONE | ICD Tables 28–39; signed 23/24 Oct | 23 Oct ✔ |
| M3 | Ground station NodeMCU-32S, USB-powered | SoW 1, ICD 3.5.1 | DONE | `groundstation_PIO` v4.1.0, G2 gate PASS | ✔ |
| M3b | Spare ground station ESP32-C3 | SoW 1, risk mitigation | IN PROGRESS | Firmware prepared 9 Nov; build, smoke test, ICD Rev B note | 24 Nov |
| M4 | Crane radio on J1 + CE J3.3, bench air link | SoW 1, ICD Table 31, Table 43 stages 2–3 | VERIFIED | Run `20261028_230328`: 0.0 % PER, RTT max 0.84 ms (limit 4.15) | ✔ |
| M5 | 32-byte protocol on all ends, ACK-payload telemetry | SoW 2, ICD Tables 33 / 34 | VERIFIED | 200/200 valid 0x81 frames in the same run; dashboard decodes every field | ✔ |
| M6 | Dashboard core: monitor + control | SoW 3, FR12, FR13, OQ3 | DONE (bench) | `dashboard/` v4.x; remaining items B1–B6 in the README | 25 Nov |
| M7 | Load cell IF13.0: ADS1220, tare, calibration, overload | ICD Tables 37–39, FR5 | DONE (bench) | Calibrate with known masses on the real hoist with MCTR 2 | 25 Nov |
| M8 | Crane outputs: lateral PWM / dir, brake, magnet PWM / logic, SPWM / enable | ICD IF1.0, IF15.0, IF12.0, IF6.0 | **FIRMWARE DONE 18 Nov (v5.0.0-rc1)**, board test from 19 Nov | Crane firmware v5 (README → *Crane MCU* C1–C5); SPWM at **10 kHz** (EEII 1 request, ICD Rev B) | 21 Nov |
| M9 | Crane sensors: encoder count, VFD + magnet current, VFD fault input | ICD Tables 13, 15, 19, 26 | **PARTLY DONE 18 Nov**: encoder, magnet current, DRV_FAULT input. **OPEN: VFD current** (EEII 1 signal format / pin) | ADC sharing (D10), PCNT encoder (D11), EEII 1 current signal | 21 Nov |
| M10 | Safety state machine: lost-link safe hold, overload inhibit, soft stop, brake timing | FR6, FR14, ICD Table 32 + operating sequence, Table 39 | **FIRMWARE DONE 18 Nov (v5.0.0-rc1)**, per module + hoist travel limits; bench test 19–20 Nov | Replaces the telemetry-only flags | 20 Nov |
| M11 | Unpowered full-group signal handshake (Table 43 stage 4) | ICD 4.2.5 stage 4 | PLANNED (`PINTEST` written 18 Nov) | `PINTEST` on the crane, scope / LED on every pin, CSV evidence | 23 Nov |
| M12 | Obstacle avoidance: auto mode with planned segments | SoW 5, ICD Table 33 b2 + `segment_idx` | PLANNED | Dashboard planner + crane segment execution | 25 Nov |
| M13 | Anti-sway: pendulum model, ZV shaper, flat-vs-shaped sway test | SoW 4, ICD 3.5.6, general requirement 3 (sway < 5°) | IN PROGRESS | S-curve done; measure sway period on the rail, add ZV | 27 Nov |
| M14 | Rail RF at full power 5–10 m, with drives running | ICD Table 29 / 43 stage 2, SoW 6 | PLANNED | `smoke_test.py --sweep` at PA MAX on the rail; `SCAN` survey at the rail | 24 Nov |
| M15 | Rail integration: each package driven from the dashboard, readings vs instruments | ICD 4.1 / 4.2, SoW 6 | PLANNED | MCTR 1 (24 Nov), MCTR 2 + EEII 2 (25 Nov), EEII 1 VFD hook-up after their sign-off (26 Nov) | 27 Nov |
| M16 | Acceptance tests: lost-link, E-stop, overload, PT1 / PT2 timing support | FR14, PHY2, ICD 4.2 | PLANNED | Test log + videos | 27 Nov |
| M17 | Firmware freeze v5.0 (crane, ground stations, dashboard) | Portfolio | PLANNED | Tagged in git | 28 Nov |
| M18 | **Demonstration** | — | PLANNED | Demo checklist (README); dry run 30 Nov | **Wed 2 Dec** |
| M19 | **Group presentation** | — | PLANNED | REII slides merged into the group deck by 1 Dec | **Thu 3 Dec** |
| M20 | **Final documentation on eFundi** (individual report, 2025 example structure, + portfolio) | — | PLANNED | §6 below | **Fri 4 Dec 08:00** |

---

## 4. Done since the last record

### 15 Oct → 29 Oct

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

### 29 Oct → 17 Nov

| Date | Work | Evidence |
|---|---|---|
| 9 Nov | Spare ground station on the ESP32-C3 (Increment 2 TX board): C3 pin block, USB-CDC host wait, build envs, rewiring / test / ICD Rev B guide | `Increment 4/groundstation_C3/` |
| 17 Nov | Documentation re-baselined to the new dates (2 / 3 / 4 Dec); open questions cut down to one firmware question list per teammate; EEII 1's 10 kHz SPWM request logged | this file, README, timesheet, `REII Open Questions and Decisions.docx` |

---

## 5. Plan to finish (17 Nov → 4 Dec)

Teammate inputs each day depends on are in §7. Section labels C1–C8, G1–G4 and B1–B6 refer to the change lists in `Increment 4/README.md`.

| Date | REII work | Done when | Needs |
|---|---|---|---|
| Tue 17 Nov | Re-baseline documentation to the new dates; send each teammate their firmware questions (§7) | Files updated; questions sent | — |
| ~~Wed 18 Nov~~ **Tue 17 Nov ✔** | **Crane C1: lateral (IF1.0)** written (v4.6.0): ENA PWM J7.9, IN1–IN4 J4.4–J4.7, encoder J7.12 → real `trolley_pos` / `trolley_vel`, 20 m rail limits, `TROLLEY` test commands | Host check + simulation pass | — |
| Wed 18 Nov | **Build + flash v4.6.0; scope every drive and trolley output** (`DRIVE SQUARE A/B/C`, `DRIVE SINE`, `TROLLEY PWM`, `TROLLEY IN FWD/REV`); measure the encoder output **before** connecting J7.12; then the MCTR 1 driver turns both ways from the dashboard | Each output on the scope / meter; wheels turn both ways | MCTR 1: encoder output type (open); driver + motors |
| Thu 19 Nov | **Joint MCU → VFD hook-up with EEII 1** (their board is ready; slot 19 Nov): low DC bus first, then the motor; hoist up / down from the dashboard. Build + flash **v4.7.0**; scope J7.5 (`MAGNET PWM`), J3.6 (`MAGNET LOGIC`); `MAGNET CAL` on the 5 V bench (magnet C3 written 18 Nov) | Brake follows the hoist command; magnet PWM / logic measured; current calibrated | MCTR 2 brake answers; clamp meter for MAGNET CAL at ≈ 11 A; 12 V supply ≥ 12 A |
| Fri 20 Nov | **Bench test of the safety behaviour (v5.0.0-rc1, firmware written 18 Nov):** unplug the GS mid-move, soft stop, Hold Brake, overload inhibit, Zero Position + hoist limits; `TLM REAL` fields | Speeds 0 ≤ 500 ms, brake clamps, magnet stays on; hoist stops at the estimated ends | — |
| ~~Sat 21 Nov~~ **Tue 17 Nov ✔** | **Crane C5: hoist drive (IF6.0)** written: 10 kHz SPWM, DRV_EN, brake, inputs, V/f, stop-then-swap reverse, test commands (v4.5.0) | Host check + simulation pass; scope check on the board still to do | MCTR 2 spool radius, gear ratio, motor nameplate (estimate only) |
| Sun 22 Nov | **`PINTEST` dry run** alone (written 18 Nov), then only `main.c` changes from the answers received; spare day | `PINTEST` steps every pin | MCTR 2 / EEII 1 answers |
| Mon 23 Nov | **Integration stage 4 (unpowered)** with all packages, run with `PINTEST`; **dashboard B1–B3** | `handshake.csv` with every pin checked | All packages' connectors on the board |
| Tue 24 Nov | **Rail session 1 (MCTR 1).** PA MAX smoke test + sweep at 5–10 m and at the far end of the rail, `SCAN` at the rail, measure the rail length, lateral motion, encoder vs tape. Spare C3 ground station smoke test (M3b) | Smoke PASS at range; rail length and position error recorded | Rail slot; MCTR 1 carriage on the rail |
| Wed 25 Nov | **Rail session 2 (MCTR 2 + EEII 2).** Load cell with known masses, brake release / clamp, magnet on / off + current. **Dashboard B4** (auto mode + obstacle planner) | 0.1 kg readings vs reference masses; brake holds; planned path runs in the bench model | Hoist on the rail, known masses, magnet |
| Thu 26 Nov | Spare day: hoist re-test with the VFD if 19 Nov needs a repeat; otherwise C7 / C8 polish | — | — |
| Fri 27 Nov | **Anti-sway (M13, B5)** at 3 rope lengths; **rail session 3**: E-stop, lost link mid-move, overload, obstacle-avoidance run | Test log + video per test; sway < 5° target | Payload on the hook; whole group |
| Sat 28 Nov | **Verification:** requirement verification matrix, PT1 / PT2 timing from dashboard CSV, **B6** session summary; **firmware freeze v5.0** (crane, ground stations, dashboard) | Matrix complete; v5.0 tagged | Test data from 24–27 Nov |
| Sun 29 Nov | **Report** chapters 1–3 (introduction, design process, trade study, calculations, implementation evidence) | Draft chapters | Figures from `Increment 3/Interface Control Documentation/System Architecture/` |
| Mon 30 Nov | **Full demo dry run** with the group; **report** chapter 4 (verification) | Dry run passes the checklist | Whole group |
| Tue 1 Dec | **Report** chapters 5–7; **REII presentation slides** merged into the group deck; demo fixes from the dry run | Report draft complete; slides in | Group deck template |
| **Wed 2 Dec** | **Demonstration** | — | — |
| **Thu 3 Dec** | **Group presentation.** Afterwards: final report read-through, PDF export, portfolio (report, timesheet, test logs, CSVs, code) | Everything ready to upload by the evening | — |
| **Fri 4 Dec, before 08:00** | **Upload the final documentation to eFundi** | Submission receipt saved | — |

**Fallbacks.** If a teammate's hardware is late, the matching crane output is verified on the bench with an LED, a scope or a logic analyser, and the field stays on `TLM MODEL` with a visible "modelled" badge on the dashboard. The demo still shows the full command path. The report states which interfaces were verified on hardware and which on the bench.

---

## 6. Final documentation (eFundi, Fri 4 Dec 08:00) — structure and sources

The individual report's chapter layout follows the 2025 example, `REII327_Final_Report_41942442.pdf`. The portfolio adds the timesheet, test logs, CSVs and code.

| Chapter (2025 layout) | REII content | Sources already in the repo |
|---|---|---|
| 1 Introduction (purpose, role in the larger system, scope) | Remote control dashboard + RF ground station for the Group 13 gantry crane | SoW, SysRS 1.1, ICD 1.1.5 |
| 2.1 Context analysis (SCD) | Operator, PC dashboard, ground station, crane controller, other packages | `Increment 3/Interface Control Documentation/System Architecture/` |
| 2.2 System architecture (physical diagram, functional units, interfaces) | FU6 ↔ FU8, IF10.0, IF-E1.0, MCU side of IF1 / 6 / 12 / 13 / 15 | ICD Fig 1, Tables 5–6, 28–34 |
| 2.3 Functional and behavioural analysis (mission, FFBD L0 / L1) | Connect → command → telemetry → safe hold | `System_FFBD.drawio` |
| 2.4 Resource mapping | Functions → ground station / crane / dashboard | this file §3 |
| 2.5 Key requirements | FR12, FR13, FR14, OQ3, DR4; SoW 1–6; ICD limits (PER < 5 %, RTT < 4.15 ms, 20–50 Hz) | SysRS, ICD Table 32 |
| 3.1 Trade study | Ground station MCU (Nano vs NodeMCU-32S vs STM32; C3 spare), channel / rate, payload profile (static vs ACK payload), CE path (J7.10 vs J3.3), load-cell path (ADS1220 vs HX711), SPWM carrier (20 vs 10 kHz), dashboard stack | Increment 2 report, ICD 3.5, README → *Locked decisions* |
| 3.2 Calculations | Air time and worst-case delivery (4.15 ms), 32-byte budgets, load-cell resolution (gain 128, mV/V), encoder counts → mm, SPWM carrier / LUT timing, pendulum period and ZV timing | ICD Tables 32–34, `loadcell.c`, `spwm.c`, `ramp.py` |
| 3.3 Implementation evidence (wiring, firmware screenshots) | Crane, ground station and dashboard screenshots, wiring photos | `Increment 4/`, dashboard sessions |
| 3.4 Alignment with requirements | Requirement → implementation table | README |
| 4 Verification (setup, results, matrix, evidence) | Smoke tests (bench + rail), dashboard sessions, safety tests, sway test, demo | `results/i4_bench/`, `results/dashboard/` |
| 5–7 Conclusions, future work, references | Including the 28 Oct link fault analysis as lessons learned | this file §4 |

---

## 7. Inputs needed from the team (firmware finalization)

Cut down on 17 Nov to only what the crane firmware still needs. The full list, with tick boxes, is in `REII Progress/REII Open Questions and Decisions.docx`.

| From | Needed | For | By |
|---|---|---|---|
| MCTR 1 (AR Cronje) | Encoder output type (push-pull 5 V or open collector) and which input circuit is fitted (**left open**; measure before connecting J7.12). *Closed 17 Nov: HW-095 wiring, "+" direction, counts per wheel turn, rail length (20 m).* | C1 encoder input | Wed 18 Nov |
| MCTR 2 (VA King) | Spool radius and total gear ratio; hoist motor poles and rated speed; brake on J4.8 (wired, HIGH = release, 50 ms lead OK); load-cell rated capacity; 2–3 known masses | C2, C5 hoist estimate, calibration | Thu 19 Nov |
| EEII 1 (MM Van Rooyen) | *Answered 17 Nov, see below.* Still open: DRV_FAULT existence / polarity; the VFD current signal: the wiring document (final pinout) gives only **J6.7 (AN2, analog)**, but EEII 1 said "digital input to his own DAC" — can it be an analog voltage on J6.7, and what scale? (The J3.8 assumption is removed: J3.8 is spare); the amplitude of his 10 kHz test signal (the MCU gives 3.3 V). Was: 10 kHz confirmed as the carrier; whether the opto stage inverts; minimum / maximum duty; where DRV_EN goes and its idle level; DRV_FAULT (exists? polarity? latched?); current signal (analog per the SoW, or digital: pin, format, scale); start / max frequency, V/f, ramp; direction change on the fly or stop first; sign-off date | C5, `vfd_current`, ICD Rev B | Fri 20 Nov |
| EEII 2 (F Prinsloo) | *Answered 18 Nov (see below).* Still to confirm on the rig: freewheel diode across the coil; 12 V supply rated ≥ 12 A; coil heating at ≈ 140 W; which sensor part (measured −0.0988 V/A vs datasheet graph ~0.185 V/A) | C3, `magnet_status` | Wed 25 Nov rail session |
| Group | ICD Rev B items (10 kHz SPWM, VFD current signal on J6.7, C3 ground station; *no E-stop input: not in the final pinout, E-stop stays HV-only*); rail slots 24, 25 and 27 Nov; VFD hook-up 26 Nov; dry run 30 Nov; presentation template | M11–M19 | Fri 20 Nov |

**Answers received (29 Oct)** — still valid

| From | Answer | Effect |
|---|---|---|
| MCTR 2 | Load cell output **2.002 mV/V**; cell already wired and connected to J6 | `RIG_LOADCELL_SENS_MV_V` set to 2.002 in `rig_config.h`; no wiring question left |
| EEII 1 | VFD board: final soldering and testing with a **50 % duty signal generator** (originally 9–13 Nov) | C5 verified on the scope first; MCU → VFD hook-up only after EEII 1 signs off (now planned 26 Nov) |
| EEII 1 | **IGBT on the DC bus with a driver and opto: logic HIGH = enable** on DRV_EN (J3.4) | Confirms D12 |
| EEII 1 | Current sensor **has a DAC and needs a digital input** | Not read on J6.7 (AIN2); pin and signal format to agree → ICD Rev B; ADC sharing reduced to load cell + magnet current (D10). Note: the EEII 1 SoW item 7 asks for an analog signal below 2.048 V on the shared ADC, so this is re-confirmed in §7 |
| EEII 1 | **10 A stops the drive** | Overcurrent fault b0 at 10 A |
| MCTR 1 | **60 mm wheel**; wheel turns 1.5 times per motor turn; **encoder on the motor shaft** | Wheel size used in C1. The ratio is the reverse of ICD Table 15 (1050 counts per wheel turn); the ICD value stays the default until the C1 test counts 10 wheel turns |
| MCTR 1 | Rail **≈ 20–30 m** (estimate) | Measure at rail session 1; RF test also at the far end; `trolley_pos` (int16 mm) covers up to 32.7 m; model rail set to 20 m |
| MCTR 1 | **One HW-095 drives both motors** from one set of MCU signals, as planned | ENB jumpered to ENA; IN3 / IN4 copy IN1 / IN2 (J4.6 / J4.7 or jumpered on the driver); one motor per driver channel |
| MCTR 1 | Asked whether the MCU has 5 V inputs | No: 3.3 V only. Push-pull encoder → 2.2 kΩ / 3.9 kΩ divider (3.2 V); open collector → 4.7 kΩ pull-up to 3.3 V, no divider (README, pin map) |

**Closed 17 Nov (MCTR 1, direction / counts / rail length)**: which end "+" goes to does not matter (perspective) — "+" = IN1 HIGH / IN2 LOW, and the rail limits are armed by zeroing at the end a "−" command reaches; counts per wheel turn stay at **1050** (ICD / wiring doc); usable rail **20 m**.

**Closed 17 Nov (MCTR 1, HW-095 wiring)**: closed by REII so it no longer blocks the firmware. One HW-095 drives both motors as in the ICD (ENB jumpered to ENA). The firmware drives IN1 / IN2 on J4.4 / J4.5 and copies them to IN3 / IN4 on J4.6 / J4.7 (D11), so either wiring of IN3 / IN4 works. One encoder only, channel A on J7.12.

**Answers received (18 Nov, EEII 2)** — bench test video, 5 V supply

| Question | Answer | Effect (firmware v4.7.0) |
|---|---|---|
| Current sensor on J6.8 | 2.57 V at 0 A (10 % duty, 0.00 A); 2.32 V at 2.53 A (99 %); no divider needed | Zero 2.57 V (auto-zero), slope −0.0988 V/A; output falls with current → 1.83 V at 7.5 A, inside 0–3.3 V |
| Holding threshold | 7.5 A | `hold_threshold_a = 7.5` (bench: `MAGNET THRESHOLD 2`) |
| MAG_LOGIC J3.6 | Enable, HIGH when engaging | Output, HIGH while engaged |
| PWM | 15 kHz; 99 % engages, 10 % disengages | 15 kHz on J7.5 (GPIO41); ON 99 %, release 10 %, boot 0 % |
| Supply / coil (final, 18 Nov evening) | 12 V supply, 99 % stays, coil ≈ 1 Ω, bench supply was current-limited | ≈ 11.4 A on 12 V (> 7.5 A, holding reached); warnings for release current > 0.5 A and over-current > 10 A |

**Answers received (17 Nov, EEII 1)**

| Question | Answer | Effect (firmware v4.5.0) |
|---|---|---|
| 10 kHz carrier for all three phases? | **Yes** | `carrier_hz = 10000` |
| Does the 4N25 stage invert? Duty limits? | **Inverts**; limits unknown | Pins inverted in the GPIO matrix; IN duty clamped to 15–85 % (assumed) |
| What does DRV_EN drive? | An IGBT with its opto that switches the circuit on and off | HIGH = on, LOW at boot and whenever stopped |
| DRV_FAULT on J3.5? | Unconfirmed | Assumed active LOW with pull-up (unconnected = no fault) |
| Current signal? | Current sensor needs a digital input to his own DAC | Wiring document (final pinout): VFD current = J6.7 (AN2, analog). Not read yet (shared ADC, C4). The J3.8 digital assumption was removed in v4.6.0 (J3.8 is spare). To reconcile with EEII 1 |
| Frequencies, V/f, ramp? | Assume from the ICD, note in `main.c` | 5–50 Hz, V/f m = 0.70 at 50 Hz (boost 0.15), 25 Hz/s, all marked ASSUMED in `main.c` |
| Swap B/C while running? | **First stop, then swap** | Full stop, ≥ 500 ms at rest, then reverse |
| Sign-off / hook-up | Board ready 18 Nov; **hook-up scheduled 19 Nov** | Plan moved from 26 Nov to 19 Nov |

**Request received (17 Nov)**

| From | Request | Effect |
|---|---|---|
| EEII 1 | **10 kHz SPWM** carrier instead of 20 kHz | `SPWM_CARRIER_HZ` 20000 → 10000 in C5 (period 4000 ticks at 40 MHz; LUT and phase step follow automatically). ICD Tables 16, 18–20 and D12 change → ICD Rev B |

---

## 8. Risks

| Risk | Effect | Mitigation |
|---|---|---|
| Crane firmware C1–C8 not started and only 15 days to the demo | Packages can't be driven from the dashboard at the demo | C1–C8 packed into 18–22 Nov, one interface per day, each verified on LEDs / scope without waiting for teammates' hardware |
| Teammate answers or hardware late (VFD board; magnet module) | MCU-driven hoisting may only be shown in the last days before the demo | Questions sent 17 Nov with "by" dates; SPWM verified on the scope on 21 Nov so hook-up is plug-in only; joint EEII 1 + REII session on 26 Nov; keep the hoist fields modelled and badged until then; report it honestly |
| 10 kHz carrier is audible and is an ICD change | Motor whine; ICD mismatch at assessment | Agree it in ICD Rev B before 21 Nov; note it in the report's trade study |
| Harness errors when packages connect their leads to the shared board (the 28 Oct link fault was a removed MISO lead) | A subsystem silently stops working | Pin-by-pin check against the wiring document before each power-on; `PINTEST` (C8) after every rewire; label every Dupont lead |
| EMI from the VFD and motors on the rail | PER rises | PA MAX at 5–10 m, antenna clear of the 220 V leads (ICD Table 29), `SCAN` survey on the rail, channel switch on the dashboard |
| ADC sharing lowers the load-cell rate or noise performance | Noisier load reading | D10 schedule keeps the load cell ≥ 20 Hz; verify noise with `LOAD WATCH`; fall back to 20 SPS if needed |
| Too little time for anti-sway and obstacle avoidance | SoW items 4–5 partly shown | Keep the S-curve as the guaranteed baseline; ZV and planner are self-contained dashboard features |
| Documentation squeezed between the demo (2 Dec) and the 08:00 deadline (4 Dec) | Weak or late report | Report chapters written 29 Nov – 1 Dec, before the demo; 3 Dec only for the read-through and upload prep; upload well before 08:00 on 4 Dec |
