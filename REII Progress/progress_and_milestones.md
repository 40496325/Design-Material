# 23 - Progress and Milestones (REII)

> LIVE project-state record. Status vocabulary and evidence rules per `.clinerules/reii_rules.md`.
> Allowed statuses: NOT STARTED / IN PROGRESS / IMPLEMENTED / TESTING / VALIDATED / BLOCKED / ACCEPTED / DEFERRED.
> Evidence rule: every status below is traced to a repository source path. Root-level curated Markdown (00-22) was used only to locate evidence, never as authority.
> Snapshot: 2026-09-24, repository commit `7bc09fc` ("new"), working tree clean. No original evidence file was modified during this audit.

## Current Project Status


* Last updated: 2026-10-15 (fifth pass) â€” visual schematic read of `ESP32S3Devboard.pdf` completed (all 5 pages rendered + coordinate-bound; generated artefacts in `_sch_render/`): the full GPIOâ†’netâ†’connectorâ†’package map is implemented as **`docs/26_controller_gpio_map.md`**, and the REII nRF24L01 wiring diagram + RF configuration plan as **`docs/27_reii_nrf_wiring_and_config.md`**. Findings: `GPIO48 = STAT_LED = WS2812B` confirmed by schematic (closes the starter's STATUS_LED TODO); nRF attach point identified as **J1 "Comms Exander"** (shared SPI2 + `SPI_xCS` GPIO17 as CSN + switched 3.3VP) â€” PROPOSED, ICD pins remain TBD; header pin budget cannot satisfy SysRS IR1â€“IR5 directly (7 free GPIOs vs â‰ˆ12 PWM-capable demands) â‡’ the MCP23017 J3/J4 + IO-board split is the open D-06 decision (reported, not resolved). First-flash evidence: the board (blank window measured 2026-10-10) now runs the starter â€” `EngDes26_start main rig micro/Port monitoring results after flash.txt` (boot 2026-10-15, ESP-IDF v6.1, I2C `0x20/0x38/0x51`, ADS1220 OK, SD timeout/no card, SPWM GPIO38/39/40 @ 20 kHz/50 Hz; **16 MB flash measured** â‡’ SysRS DR5 N8R2 conflict remains open). Previously 2026-10-10 (fourth pass) - created `docs/25_open_questions.md` (routing document Q-REI-001..20, Q-MCT-001..20, Q-MCTG-001..20, Q-EEDI-001..20, Q-EERM-001..15, Q-LEC-001..15) â€” all open questions routed by owner. Increment 3 phases in `docs/06_build_plan.md` are the current development plan.
* 2026-10-09 direction + cleanup update (Act mode, user-confirmed FACT): **STM32F411 dongle scrapped** â€” status SUPERSEDED, no further STM32 work; groundstation is now an ESP from the RF-characterisation pool (recommended: NodeMCU-32S, see `docs/03_state.md` + `docs/06_build_plan.md` D-08/D-09). Main controller board received pre-programmed (`project_sources/ESP32 S3 WROOM-1U/` photos + partial `firmware_backup.bin` + esptool note); S3 source + schematics still pending so S3 integration stays BLOCKED. Markdown sprawl condensed: 6-file set in `docs/` (`01_project`, `02_rules`, `03_state`, `04_rf_baseline`, `05_protocol`, `06_build_plan`); 27 superseded root Markdown files archived to `docs/_archiv

* Current increment: **Increment 3 (I3)** - first physical integration phase, planned Weeks 7-10.
  * Evidence: `project_sources/03_increment_3/WEEK_PLAN.md` and `project_sources/03_increment_3/ACTIONS_NEEDED.md` (both state "I3 = first physical integration on rail (Weeks 7-10)").
  * Date anchors: `project_sources/05_slides/7.1 EngDes26 - Checkpoint Breakdown.pptx` - "11 Sep: Individual checkpoint due", "14 Sep: Increment 3 begins - group ICD drafted & signed off in week 1", "27 Oct: Participation marks captured", "13 Nov: Final Demonstration", "16 Nov: Final Group Presentation & Individual Portfolio".
* Current development phase: evidence-baseline establishment. Increment 2 nRF24L01 bench characterisation has executed measurements; no rail / on-crane integration evidence is present in the repository.
* Last updated: 2026-10-05 (third pass) - `00_README.md` restructured into the central project dashboard: gist, status board, decisions needed, open questions for team/lecturer, and an outside-Cline action guide (What/Prepare/How) - see the third-pass bullet under Completed Since Last Update. Pr
* 2026-10-27 (Act mode) - Dashboard backend/frontend implemented (Architecture A1, D-10):
  - `project_sources/03_increment_3/dashboard/app.py` â€” Flask + SocketIO backend, serial reader integration, WebSocket command/telemetry events
  - `project_sources/03_increment_3/dashboard/protocol.py` â€” 32-byte pack/unpack (DownlinkFrame/UplinkFrame, PROVISIONAL ICD Â§4 maps per D-01)
  - `project_sources/03_increment_3/dashboard/serial_reader.py` â€” USB-CDC serial reader parsing groundstation `ACK,<ok>,payload=<hex>` / `PONG` / `ECHO` lines
  - `project_sources/03_increment_3/dashboard/templates/index.html` â€” telemetry dashboard + command panel + event log
  - `project_sources/03_increment_3/dashboard/static/app.js` â€” WebSocket client, real-time UI updates
  - `project_sources/03_increment_3/dashboard/static/style.css` â€” dark-theme dashboard styles
  - `project_sources/03_increment_3/dashboard/requirements.txt` â€” Python dependencies
  - `project_sources/03_increment_3/dashboard/INTERFACE_MATRIX.md` â€” interface matrix for all REII boundaries
  - DECISION_LOG.md D-10 updated: PROPOSED â†’ IMPLEMENTED
  - 23_progress_and_milestones.md: Dashboard row â†’ IMPLEMENTED; Section 8 updated with implementation details
  - Status: IMPLEMENTED (code complete), not yet VALIDATED (awaiting live COM12 serial test)eviously 2026-10-05 (second pass) - the REII entries in `Group_milestones.xlsx` were decomposed into explicit incremental gates (all dates unchanged; every REII working gate carries `(planned, not agreed)`), see the gate-decomposition bullet under Completed Since Last Update. Previously 2026-10-05 - REII milestones added to the shared group calendar `Group_milestones.xlsx` (17 dated entries, plus repair of the broken `November!G17` date cell); evidence and status recorded under "Completed Since Last Update". Previously 2026-09-25 - REII application-protocol design proposal drafted as `24_application_protocol_design_proposal.md` (Sec 1-20); status of that artefact is PROPOSAL only. Previously updated 2026-09-24 (initial population of this record from repository evidence). A previous `23_progress_and_milestone.md` was removed in commit `7bc09fc`; no prior validated record exists, so no historical progress is asserted here.
* Overall status: **IN PROGRESS**. Only the RF bench characterisation sub-project has measured evidence. STM32F411 dongle, ESP32-S3 integration, dashboard, anti-sway, obstacle planning and rail validation: NOT STARTED per repository evidence. Application protocol: no longer blocked on the absence of a proposal - `docs/_archive/root/24_application_protocol_design_proposal.md` now contains a PROPOSED 32-byte downlink/uplink definition (Sec 10/11) awaiting cross-package review; the ICD Sec 4 freeze remains open and nothing is validated or accepted.
* 2026-10-09 direction + cleanup update (Act mode, user-confirmed FACT): **STM32F411 dongle scrapped** â€” status SUPERSEDED, no further STM32 work; groundstation is now an ESP from the RF-characterisation pool (recommended: NodeMCU-32S, see `docs/03_state.md` + `docs/06_build_plan.md` D-08/D-09). Main controller board received pre-programmed (`project_sources/ESP32 S3 WROOM-1U/` photos + partial `firmware_backup.bin` + esptool note); S3 source + schematics still pending so S3 integration stays BLOCKED. Markdown sprawl condensed: 6-file set in `docs/` (`01_project`, `02_rules`, `03_state`, `04_rf_baseline`, `05_protocol`, `06_build_plan`); 27 superseded root Markdown files archived to `docs/_archive/root/`; 6 superseded `project_sources/03_increment_3/*.md` planning files archived to `docs/_archive/increment3/` (history only, not authority). RF bench rig, results, photos, PDFs, `.clinerules/reii_rules.md` untouched.

## Milestone Progress

| Milestone / Work Package | Status | Evidence | Outstanding Work |
| --- | --- | --- | --- |
| Normative requirements baseline (SysRS + 5 SoWs) | IN PROGRESS | `project_sources/00_requirements/EngDes26 - SysRS v1.0.pdf` (17 pp, Rev 0 "for discussion", 2026-08-04, PT3 threshold [TBD], Sec 5.1 verification methods deferred to "next revision"); 5 work-package SoW PDFs; `general_requirements.txt` (unattributed, conflicts - see Contradictions) | Obtain SysRS revision with Sec 5.1 verification methods + PT3 value; confirm binding document set for I3 |
| System architecture models (FFBD + Sys Architecture) | IN PROGRESS | `project_sources/01_system_architecture/System_FFBD.drawio` (REII functions 3.2.x/3.3.x/3.5.x, abort 3.6, shutdown 3.7/3.8); `Sys_Architecture.drawio` (F/U 6.0 Remote Controller = 6.1 dashboard app + 6.2 dongle MCU + 6.3 nRF24 + 6.4 antenna + 6.5 USB bus power; I/F C.1-C.5) | Align to signed ICD v1.0; export/verify PNG views; reconcile interface numbering against SysRS Sec 4.1/4.5 |
| Interface Control Document (ICD_REII) | IN PROGRESS | draft `project_sources/02_increment_2/ICD_REII.pdf` + `.docx`: Sec 1-3 defined (USB-bus-powered STM32F411 dongle; ESP IO header pins TBD; 10 uF + 100 nF; 3.3 V only; ch 77 / 2 Mbps / PA_MAX / ARC 5 / ARD 500 us / 5-byte address / CRC-16; ShockBurst Auto-ACK, ACK-payload uplink, ~4.1 ms worst case); Sec 4 = 32-byte payloads TBD [ACTION REQUIRED] | Freeze Sec 4 byte maps; finalise ESP IO-header SPI pins; obtain signatures; no signed ICD file exists in the repository |
| RF characterisation - bench measurement campaign | TESTING | `project_sources/02_increment_2/rf_characterisation/results/` - `packet_loss_2026-09-07.csv` (90 rows, 1-190 m LOS, ch 18), `packet_loss_2026-09-08.csv` (24 rows, 5 m, ch 2/4/6/11), 9 channel-survey CSV/PNG/RAW sets, `summary.md`; `results/old/` earlier captures; `docs/`; `tools/`; 6 PlatformIO envs in `firmware/platformio.ini` | Fill `docs/05_recommended_config.md` measured/decision columns (still `___`); bench smoke at ICD config (ch 77 / 2 Mbps) not evidenced; per-point n = 1 (no repeatability runs) |
| RF characterisation report (I2 deliverable) | IMPLEMENTED | `project_sources/02_increment_2/rf_characterisation/REII327 nRF24L01 Link Bring-Up & Characterisation 40496325.docx` + `.pdf` (Tables 1-2, Figures 1-6); duplicate set under `.../submission/` | No marking/acceptance evidence in repository. `Evidence not available - status remains TBD.` |
| ESP groundstation (ESP, ex-STM32F411 dongle) | IMPLEMENTED (untested scaffold 2026-10-09) | Scaffold `project_sources/03_increment_3/groundstation_esp/` (`platformio.ini` env `groundstation_nodemcu32s`, `src/groundstation.h/.cpp` NodeMCU pins + app/control init tables, `src/main.cpp` USB-CDC bridge `INFO/PING/APP/CTRL/CMD/ECHO/HELP`, `README.md` bring-up order); bench rig untouched; no build/flash/test evidence yet (`pio` missing); ECHO+INFO serial captures already in `project_sources/03_increment_3/results/i3_bench/ECHO.txt` and `ECHO+INFO.txt`; PINGâ†’PONG confirmed live by user on 2026-10-16 | Install PlatformIO â†’ build â†’ flash â†’ ECHOâ†’INFOâ†’PINGâ†’APP smokeâ†’CMD loopback with evidence to `results/i3_bench/`; evidence-capture script added at `project_sources/03_increment_3/tools/capture_evidence.py` (dual-port, timestamps, smoke CSV, index.json, SUMMARY.md) |
| ESP32-S3 controller RF integration | IN PROGRESS (carrier bring-up) / BLOCKED (RF link) | Board flashed + measured 2026-10-15 (`EngDes26_start main rig micro/Port monitoring results after flash.txt`: IDF v6.1, app `f465a43-dirty`, I2C 0x20/0x38/0x51, ADS1220 OK, SD timeout/no-card, SPWM GPIO38/39/40 running, **16 MB flash**); schematic fully read â†’ `docs/26_controller_gpio_map.md`; nRF plan â†’ `docs/27_reii_nrf_wiring_and_config.md` (J1: CSN=GPIO17, CE=GPIO10, IRQ not used â€” STATUS polled; PROPOSED); **REII GPIO lock-in applied** â†’ `EngDes26_start main rig micro/GPIOS/26_esp32_s3_gpio_proposal_locked.html` (GPIO 10=CE locked, GPIO 17=CSN locked, GPIO 11/12/13 shared SPI2 locked, GPIO 47 power gate locked, GPIO 18 freed from IRQ) | Controller Spec still missing; VFD/magnet/encoder/brake/expansion allocation = group ICD (D-06); RF24 driver IMPLEMENTED (unbench-tested); historic-only pre-flash dump; 16 MB-vs-DR5-N8R2 variant conflict |
| Application protocol (32-byte downlink/uplink) | IN PROGRESS | ICD v1.1 DRAFT created at `project_sources/02_increment_2/ICD_REII_v1.1_draft.md` â€” consolidates D-00aâ€“d, D-08, D-09; amends Sec 1 (STM32â†’ESP groundstation, J1 pins PROPOSED), locks Sec 2 RF baseline, defines Sec 3 frame structure (0xA5 magic + type byte), PROPOSES Sec 4 32-byte downlink/uplink maps with alternatives analysed, adds Sec 5 USB-CDC link format, Sec 6 decision cross-reference, Sec 7 evidence/acceptance criteria, Sec 8 open items. Still awaiting D-01 cross-package freeze before byte maps are authoritative. | Cross-package decision (D-01): circulate ICD v1.1 Â§4 to MCTR-I/II + EEII-I/II + controller owner, close R12-R16, then freeze or mark PROVISIONAL with owner + date; byte-exact `static_assert` build + loopback pack/unpack evidence; rail command/telemetry exchange |
| Dashboard software (FR12 / FR13) | IMPLEMENTED | `project_sources/03_increment_3/dashboard/` (app.py, protocol.py, serial_reader.py, index.html, app.js, style.css, requirements.txt); DECISION_LOG.md D-10 marked TESTING (firmware flash + live serial COM12 test pending) | Stack selected (A1 Python/Flask+SocketIO); pack/unpack implemented (protocol.py, PROVISIONAL ICD Â§4 maps); serial reader implemented (serial_reader.py); WebSocket frontend implemented; awaiting live COM12 serial test for VALIDATED |
| Anti-sway motion-profile modelling (SoW item 4) | NOT STARTED | SoW intent only; `DECISION_LOG.md` D-04 status "proposed" | Pendulum/hoist model, shaping law, parameter set, measured sway result on rail |
| Obstacle-avoidance path planning (SoW item 5) | NOT STARTED | SoW intent only; ICD Sec 4.1 lists it as an open payload question | Approach, obstacle-map source, command format, evidence |
| Rail integration (I3 exit gate) | NOT STARTED | `TEST_LOG.md` rail rows blank; no rail CSV/plots/video in repository | 5/10 m rail runs, NLOS point, survey at dongle position, INTERFACE_MATRIX pass or NCRs |
| Safety / interface verification (isolation, HW e-stop chain) | NOT STARTED | PHY2 + PHY3 (SysRS Sec 4.8); EEII-I SoW element 5; `7.1 ... Checkpoint Breakdown.pptx` slide 7: "bench-verify isolation before live EE1 gate drivers" | Isolation bench verification; confirm HW e-stop path ownership; dashboard E-stop flag is NOT the safety stop (V-03) |
| I3 -> I4 handoff pack | NOT STARTED | `project_sources/03_increment_3/I3_HANDOFF.md` artefact paths marked "TBD fill Week 8/9" | Frozen headers, init tables, builds, sway model, rail evidence, signed NCR list |
| REII design report generator | IMPLEMENTED | `build_reii_report.py` (~890 lines): reportlab template + drawing helpers (RectBlock/CanvasDiagram/Connector), `make_table`, `tag`, `ReportDoc`, title page, doc control, contents, and all sections 1-7 + appendices A.1-A.3. Generates `REII_Design_Report_2026.pdf` (16 pp; 18 captioned tables, 4 figures). NOTE: the working source was lost in a cleanup wildcard (was never committed to git); it was reconstructed 2026-10-09 from the surviving PDF output and re-verified end-to-end. | Cross-package review + acceptance of the DRAFT report; the PDF output is IMPLEMENTED but the report is not VALIDATED/ACCEPTED (no signed ICD; payload maps TBD) |

| Course process artefacts (documented alternative analysis) | NOT STARTED | `EngDes26 - Analysis of Alternatives.pptx` requires criteria selection, scoring and documented justification (slides 27-28), and warns that MCDM on a mandated choice (e.g. MCU or motor type) will not be graded; no REII AoA artefact found in repository | Produce REII AoA for genuinely open choices (protocol framing/format per SoW item 2, dashboard software stack, sway-shaping approach) if required by course |

## Completed Since Last Update

* **2026-10-15 (fifth pass) â€” controller GPIO map delivered.** Visual read of
  `ESP32S3Devboard.pdf` pp 1â€“5 (PyMuPDF renders in `_sch_render/`; nets bound by PDF word
  coordinates, cross-checked against `main.c:84-126` and the measured boot log) produced
  **`docs/26_controller_gpio_map.md`**: all module signals â†’ net â†’ connector â†’ package;
  J1/J7/J3/J4/J6/J8/J5/J2/J9 pinouts; I2C/SPI/MCPWM/USB/power trees; SysRS IR1â€“IR5 routing
  matrix with PROPOSED/TBD labels; pin- and ADC-budget arithmetic (reported, not resolved);
  8 conflicts/cautions incl. the net-`IO6`/GPIO1 anomaly and the +5 V/+3.3VP adjacency on J1.
  Context: SysRS Â§4.1 (IR1â€“IR5), SoW controller-interface items, ICD draft Â§1 ("ESP IO
  header"). Discharges the "visual J1/J3/J4/J6/J7 pinâ†’netâ†’GPIO table" outstanding action at
  analysis level (board-owner confirmation still open).
* **2026-10-15 â€” REII nRF wiring diagram + configuration plan delivered.**
  **`docs/27_reii_nrf_wiring_and_config.md`**: J1-attach wiring (CSN = GPIO17 `SPI_xCS`;
  shared SPI2 = GPIO11/12/13; VCC = J1.8 +3.3VP gated by GPIO47; mandatory 10 ÂµF + 100 nF at
  the module; never J1.7/+5 V); CE/IRQ options Aâ€“D (contested with the MC2 allocation â€”
  reported, not resolved); register-level configuration (application baseline ch 77 / 2 Mbps /
  PA_MAX / ARC 5 / ARD 500 Âµs / 5-byte / CRC-16 / 32 B vs control fallback ch 80 / 250 k /
  ARC 15 / ARD 1500 Âµs kept separate); pipes `REII1`/`REII2`; latency cross-check â‰ˆ 4.15 ms
  vs ICD "~4.1 ms"; evidence-gated 8-step bring-up plan. Status: **PROPOSED** pending ICD
  signatures and measured bring-up.
* **2026-10-15 â€” first flash / carrier evidence (supplied outside Cline):**
  `EngDes26_start main rig micro/ESP_Build Terminal results.txt` (`Project build complete`,
  IDF v6.1, app `0x4d420` B = 70 % of 1 MB partition) and
  `EngDes26_start main rig micro/Port monitoring results after flash.txt` (boot 2026-10-15
  19:22, app `f465a43-dirty`, I2C `0x20/0x38/0x51`, ADS1220 sane, SD `ESP_ERR_TIMEOUT`
  (no card fitted), SPWM started on GPIO38/39/40 @ 20 kHz carrier / 50 Hz sine, WS2812 task
  started, **16 MB flash**, console-line vs USB-COM12 ambiguity recorded as an open check).
  GPIO48 â†’ `STAT_LED` â†’ WS2812B confirmed by schematic â€” closes the starter's STATUS_LED
  TODO (`README_TBD.md`).

* **2026-10-16 â€” REII bench evidence-capture scripts delivered.**
  `project_sources/03_increment_3/tools/capture_evidence.py` â€” dual-port serial capture
  that runs ECHOâ†’INFOâ†’PINGâ†’APP on the NodeMCU-32S ground station, captures the ESP32-S3
  rig's nRF24 link-up banner and PING receipt, then runs an N=200 PING/PONG smoke burst
  at the app config (ch77/2M/PA_MAX/ARC5/ARD500). Writes timestamped `groundstation.log`
  + `rig.log`, per-round `smoke.csv`, machine-readable `index.json`, and human-readable
  `SUMMARY.md` into `project_sources/03_increment_3/results/i3_bench/<run_id>/`.
  Companion `capture_evidence.ps1` PowerShell wrapper and `README.md` usage notes also
  added. This directly supports the I3 bench-exit gate in `docs/27_reii_nrf_wiring_and_config.md`
  Â§5 steps 1-5 and the `INTERFACE_MATRIX.md` "Sec 2 RF bench" row (results/i3_bench/*.csv).
  **Note:** the script is ready to run; actual bench execution (both ports connected and
  flashing completed on the S3 side) remains outstanding â€” `pio` not yet installed on the
  groundstation laptop, and the S3 RF24 driver build/flash has not yet been executed.

* **2026-10-16 â€” REII ICD v1.1 DRAFT delivered.**
  `project_sources/02_increment_2/ICD_REII_v1.1_draft.md` â€” elevates the unsigned ICD_REII v1.0 draft
  with all consolidated bench-level decisions (D-00aâ€“d, D-08, D-09). Sec 1 amended: STM32F411
  dongle â†’ NodeMCU-32S groundstation (D-09), J1 "Comms Exander" pin map PROPOSED for crane (D-06
  pending). Sec 2 RF baseline locked (ch77/2M/PA_MAX/ARC5/ARD500us/5B/CRC-16). Sec 3 defines
  provisional frame structure (0xA5 magic + type byte, PING/CMD/ACK-telemetry). Sec 4 PROPOSES
  32-byte downlink/uplink byte maps with alternatives analysed (fixed struct chosen over TLV/nanopb,
  fixed-point over float, error_code + fault_flags over single byte). Sec 5 adds USB-CDC serial link
  format. Sec 6 cross-references all decisions. Sec 7 lists evidence/acceptance criteria for signature.
  Sec 8 lists open items blocking signature. **Status:** DRAFT â€” awaiting D-01 cross-package payload
  workshop before Â§4 byte maps are authoritative.

* **2026-10-16 â€” D-01 workshop email + DECISION_LOG.md created.**
  `project_sources/03_increment_3/D-01_payload_workshop_email.md` â€” copy/paste-ready email to
  MCTR-I/II + EEII-I/II + controller owner with agenda, REII proposed maps, alternatives already
  analysed, and requested responses. `project_sources/03_increment_3/DECISION_LOG.md` â€” new decision
  register with D-00aâ€“d (accepted), D-01 (payload freeze, OPEN), D-02 (ACK-payload, PROPOSED),
  D-03 (rate policy, PROPOSED), D-04 (S-curve, PROPOSED), D-06 (IO-board pins, OPEN), D-07 (RF
  re-survey, PROPOSED), D-10 (dashboard stack = Python web, PROPOSED). Dashboard stack rationale:
  Python already in toolchain, web frontend needs no client install, fastest path to FR12/FR13.


* **2026-10-21 â€” REII GPIO lock-in proposal generated.** New file `EngDes26_start main rig micro/GPIOS/26_esp32_s3_gpio_proposal_locked.html` â€” same visualization format as existing GPIO HTML files, with REII lock-in decisions applied per Group Report v3 + docs/26_controller_gpio_map.md + docs/27_reii_nrf_wiring_and_config.md:

  **Locked (was PROPOSED/CONTESTED):**
  - GPIO 10 â†’ LOCKED to REII as nRF CE (J7.10) â€” resolves CONTEST with MC2 per Group Report Table 43 (J7.10 on SPI2 bus header); docs/27 Â§3 Option B (CE=GPIO10, IRQ=none, poll STATUS)
  - GPIO 17 â†’ LOCKED to REII as nRF CSN (J1.1) â€” confirmed by Group Report Table 43 ("CSN=GPIO17")

  **Already locked / verified (unchanged):**
  - GPIO 11/12/13 â†’ SPI2 MOSI/SCK/MISO (shared nRF) â€” MEASURED + SHARED â†’ REII
  - GPIO 47 â†’ +3.3VP power gate â€” VERIFIED + CRITICAL â†’ REII

  **Freed / released from REII:**
  - GPIO 18 â†’ IRQ disconnected â€” polled STATUS used instead per Group Report Table 43 ("J7.9 (GPIO18) stays free for MCTR 1"); pin released to MCTR 1

  **Retained as PROPOSED fallback (not locked):**
  - GPIO 16 â†’ nRF CE fallback candidate

  Evidence: Group Report v3 (DOCX) Table 43 Interface Stage 3 + Table 44 Milestone 3; docs/26 GPIO map; docs/27 wiring plan.

* **2026-10-19 â€” REII entry to the Engineering Design group report generated.**
  `Group report/REII Entry - Group Report.docx` (199 paragraphs, 28 tables) â€” REII work-package
  entry built on the group report template (styles, A4 geometry, single-section footer preserved).
  Sections: 1. Overview (incl. REII327-example content correction), 2. Interface control
  (document control, system overview, interface inventory, I/F C.1â€“C.5 + air-link specs,
  application-layer frame + proposed 32-byte downlink/uplink maps, operating sequence,
  requirements traceability, fault handling, verification, open-issues register M-1â€“M-12,
  approval, acronyms), 3. FFBD (verified 3.x numbering from System_FFBD.drawio), 4. Integration,
  5. Figures required (placeholders REII-1â€“REII-5 for user insertion), 6. Source documents.
  Identifiers written out in words; symbols as Unicode (Ã— â†’ Âµ â‰¥ â‰¤ Â° â€“). Build script
  `_build_reii_final.py` retained at repo root for regeneration. **Status:** DRAFT documentation â€”
  not VALIDATED/ACCEPTED (no signed ICD; D-01 byte-map freeze outstanding; figures not yet pasted).

This is the initial population of this record, so nothing is reported as "completed" merely because it exists. The following artefacts are present in the repository and their content was verified in this audit:

* Normative document set retrieved and machine-verified: SysRS v1.0 (17 pp), 5 work-package SoWs (1 p each) - `project_sources/00_requirements/`.
* ESP32-S3 dev-board overview retrieved and verified (4 pp): `project_sources/00_requirements/esp32-s3-dev-board-overview.pdf`.
* ICD draft DOCX and PDF found to be textually consistent (Sec 1-3 defined, Sec 4 TBD): `project_sources/02_increment_2/ICD_REII.docx|.pdf`.
* Payload-agreement meeting agenda DOCX and PDF verified (options + pros/cons only; no selections recorded): `project_sources/02_increment_2/Meeting Agenda_ Data Payload Agreements.docx|.pdf`.
* RF characterisation measured datasets present and readable (CSV schemas verified; values re-read from the files): `project_sources/02_increment_2/rf_characterisation/results/`.
* RF characterisation report verified (own words, Tables 1-2, Figure list): `.../REII327 nRF24L01 Link Bring-Up & Characterisation 40496325.docx` (107 paragraphs, 2 tables) and matching `.pdf` (12 pp).
* RF test firmware verified as implemented (6 PlatformIO envs; role dispatch; static 32-byte payload; control channel 80; ARC 15 / ARD step 5; SPI 4 MHz; per-board pin maps): `.../firmware/` and `.../firmware/lib/linktest/`.
* Architecture models verified as machine-readable Draw.io (122 valued cells Sys_Architecture; 172 valued cells System_FFBD), incl. the REII functional decomposition and the F/U 6.0 Remote Controller breakdown: `project_sources/01_system_architecture/`.
* Increment 3 planning pack present and read: `project_sources/03_increment_3/ACTIONS_NEEDED.md` (D-01..D-06, F-01..F-09), `DECISION_LOG.md` (D-00a..d, D-01..D-07), `INTERFACE_MATRIX.md`, `TEST_LOG.md` (empty measurement rows), `I3_HANDOFF.md`, `WEEK_PLAN.md`.
* Latest two slide decks inspected slide-by-slide (content, not filenames): `project_sources/05_slides/EngDes26 - Analysis of Alternatives.pptx` (28 slides) and `project_sources/05_slides/EngDes26 - Checkpoint2.pptx` (12 slides). Also `7.1 EngDes26 - Checkpoint Breakdown.pptx` (8 slides, incl. the REII checkpoint task, controller pin budget, and the semester key dates).

Work completed since the initial population (2026-09-25):

* `24_application_protocol_design_proposal.md` created and completed to Sec 1-20 (proposal only, not an interface freeze). Contents: sourced constraint list (Sec 2), requirement-to-field mapping (Sec 4), Q1-Q15 open questions (Sec 5), 9 alternative pairs (Sec 6), weighted criteria C1-C9 (Sec 7), trade study with marginal flags (Sec 8), candidate downlink map (Sec 10), candidate uplink map (Sec 11), encoding/enum tables (Sec 12), version/sequence validation (Sec 13), reliability model addressing the FFBD-vs-ICD wording clash (Sec 14), safety boundary (Sec 15), MCU suitability (Sec 16-17), 32-byte arithmetic proof (Sec 18), R12-R16 approval list (Sec 19), PROPOSED ICD Sec 4 text (Sec 20).
* Authority correction applied to `.clinerules/reii_rules.md` Sec 13 and `19_source_authority.md`: SysRS = primary system-requirements authority; each SoW = authoritative for its own scope/work allocation; ICD = interface authority once signed (draft/proposal while unsigned); conflicting sources must be reported, not silently resolved.
* `Actions needed.pdf` inspected and found to be a vector-outline PDF with no machine-readable text layer; its equivalence to `project_sources/03_increment_3/ACTIONS_NEEDED.md` is therefore **unverified**. `Evidence not available - status remains TBD.`
* 2026-10-05 - `Group_milestones.xlsx` (shared group milestone calendar, Sep-Nov 2026) updated with REII milestones; status of this artefact: IMPLEMENTED as documentation (no project validation implied).
  * 17 dated `REII:` entries written: course/group **anchors** (11 Sep individual checkpoint; 14 Sep I3 begins / ICD sign-off week 1; 9 Oct controller-board delivery estimate; 26 Oct group first integration test; 27 Oct participation marks; 6 Nov integration successful; 9 Nov finalize all ICDs; 11 Nov reports + portfolio) and REII **working milestones** (ICD Sec 4 32-byte payload freeze, STM32F411 dongle bring-up, dashboard pack/unpack + fault harness, bench ch77/2 Mbps smoke test, anti-sway model + profile generator, on-rail integration via `INTERFACE_MATRIX.md` rows, rail sway measurement, obstacle/path planning, I3 handoff pack + demo evidence) dated 14 Oct - 10 Nov.
  * Label rule applied: every REII working entry carries the literal suffix `(planned, not agreed)`. Their dates are **derived by REII** to align with the group-agreed 26 Oct first integration test and 6 Nov integration-successful entries; `project_sources/03_increment_3/WEEK_PLAN.md` Weeks 7-10 carry no calendar dates and no cross-package agreement exists for these dates - status: PROPOSED pending group review. Evidence source for anchors: `project_sources/05_slides/7.1 EngDes26 - Checkpoint Breakdown.pptx` slide 8 (key dates), `EngDes26 - Checkpoint2.pptx` slide 4 (delivery estimate 9 Oct 2026), and the existing group entries in the calendar itself.
  * Styling convention: new REII cells use Seaford 10 pt green (`FF00B050`), matching the per-package colour convention (MCTR1 black, EEII1 blue `00B0F0`, EERI purple/red); REII lines appended to shared multi-package cells keep that cell's existing colour.
  * Defect repaired: `November!G17` contained a literal 0 (rendered 1900-01-01); restored to `=E17+1` per the pattern on all other sheets; now recalculates to 2026-12-01.
  * Verification performed: file edited through desktop Excel (COM) so all workbook parts are preserved (part-name diff vs `Group_milestones.backup_2026-10-05.xlsx`: none lost, none added); cell-by-cell diff = exactly 18 changed cells (17 entries + G17 repair), zero unexpected changes and all pre-existing entries byte-identical; each entry's date cell recalculated and confirmed in Excel (17/17 exact dates); merged ranges, conditional formatting (2/sheet), data validations (7/sheet) and the `start_day` defined name unchanged; formula count unchanged except the +1 from the G17 repair.
* 2026-10-05 (second pass) - `Group_milestones.xlsx` REII entries decomposed into explicit incremental gates at **unchanged dates** (user-approved Option B); status: PROPOSED for the working gates (none of these dates is cross-package agreed), anchors unchanged. Evidence: cell-by-cell diff vs `Group_milestones.backup_pre-gates_2026-10-05.xlsx` = exactly 16 changed cells (10 overwritten, 2 new, 2 shared-cell line replacements, 2 shared-cell appends), zero unexpected changes; no workbook parts lost; merged ranges/CF/DV/defined names/formula counts identical; every composed value re-read after save with full line counts and bullet characters intact.
  * Scope correction recorded: the REII supply package is **not** limited to "change nRF configs at codebase level" on a pre-programmed controller. Per `project_sources/00_requirements/EngDes26_SoW_REII_Remote_Control_Dashboard.pdf` element 1, REII must *complete the stubbed nRF24 SPI driver on the ESP32 controller side* (plus the from-scratch STM32F411 dongle), and `project_sources/06_reference_material/What Exactly You Need to Start Doin.txt` Step 3 makes "ESP32 radio init + RX mode" REII's first integration milestone. The calendar now reflects this.
  * Gate decomposition applied (dates kept): 9 Oct input anchor reframed as controller-delivery input; **11 Oct (new, Oct!C12)** Controller Spec + ESP32 codebase obtained/board powered; **16 Oct (appended to Oct!O12)** ESP32 nRF24 stub complete - RX mode + Table 2 SPI read-back; **19 Oct (Oct!E15)** dongle split into CDC echo / SPI+radio init read-back / USB<->RF bridge; **20 Oct (Oct!G15)** dashboard split into command execution / telemetry no-sequence-gaps / fault-display capture (labelled internal target, not a SysRS bound); **22 Oct (Oct!K15)** bench exit gate for 26 Oct with PING->PONG smoke CSV + 32-B byte-exact loopback + NCR fallback; **25 Oct (new, Oct!C18)** pre-integration gates = isolation bench-verify + HW E-stop path before live drivers (7.1 slide 7) and MCTR-I profile format D-04 + encoder handoff; 26 Oct group integration test (unchanged); **27 Oct (Oct!G18)** participation evidence with scope flagged TBD (F-08 open); **28 Oct (Oct!I18)** anti-sway model + sway-frequency identification with S-curve fallback (D-04); **30 Oct (Oct!O18)** rail gates = RF re-survey + group channel plan / 5-10 m N=1000 ACK runs + NLOS / evidence paths; **4 Nov (Nov!I6)** rail sway flat vs shaped with "pass bound = internal, not SysRS" (5 deg is not a SysRS requirement - `04_verified_requirements.md`); **5 Nov (Nov!K6)** obstacle gate = map source cited (SoW 5 source undefined) + planner output in MCTR command format + run-vs-model evidence; 6 Nov integration-verified gate now requires INTERFACE_MATRIX all-pass or NCRs with owners; 9 Nov ICD sign unchanged; **10 Nov (Nov!G9)** handoff gate = INTERFACE_MATRIX + TEST_LOG filled + NCR list + demo run-through; 11 Nov report/portfolio unchanged; **12 Nov (appended to Nov!K9)** demo dry-run + evidence pack check; 13/16 Nov anchors unchanged.
  * Feasibility risks carried (not resolved by the calendar): main control board is only an *estimate* (9 Oct, `EngDes26 - Checkpoint2.pptx` slide 4) and arrives partially assembled; Controller Specification + ESP32 source are absent from the repository (F-01); IO-board owner/pins (D-06), telemetry path (D-02), rate policy (D-03), profile format (D-04) remain open cross-package decisions; 27 Oct participation scope, 13 Nov demo scope and 16 Nov presentation scope are lecturer questions (F-08); the 4-6 Nov sequence has no contingency; no rail slot dates are recorded anywhere ("Weeks 9-10" only).
  * Tooling note (evidence trail): the first write attempt silently dropped all bullet lines due to PowerShell's comma operator binding tighter than `+` inside `@(...)` array literals - concatenations were split into separate array elements and Excel received only the first line. Detected by post-write read-back, root-caused with an isolated repro, fixed by parenthesising every concatenation, and re-verified (line counts + `Â·` characters present in all 11 affected cells).
* 2026-10-05 (third pass) - `00_README.md` rewritten from a 16-line source-grounding note into the central project dashboard (182 lines, sections 1-6); status: IMPLEMENTED as documentation (derived from existing evidence, no new engineering claims). Contents: (1) 60-second gist including the REII SoW seven-element supply list, the FR14/PHY2 safety boundaries and the stand-in-hardware pin warning; (2) status board - 11 areas with status + evidence pointer (requirements, models, ICD, RF characterisation, recommended-config fill, protocol, dongle, ESP32-S3, dashboard, anti-sway/obstacle, rail, calendar); (3) hard-date list incl. past anchors struck through and the REII gate sequence; (4) decisions-needed table - C0, D-01..D-07, V-01..V-03 with blocks/owners/status plus secondary V-04..V-08; (5) open questions split into 8 for the team (sourced from `ACTIONS_NEEDED.md` Week 7 asks + calendar needs) and 8 for the lecturer (F-08, C0, F-01, D-06, V-01); (6) a nine-item outside-Cline action guide in What/Prepare/How form tied to calendar gates - payload workshop (14 Oct), Controller Spec/codebase retrieval (11 Oct), board receipt + soldering (9 Oct), RF bench smoke (pre-26 Oct), isolation verification (25 Oct), rail booking + channel plan (30 Oct-6 Nov), participation evidence (27 Oct), demo dry-run/demo/presentation prep (12/13/16 Nov), one-time tooling setup incl. dashboard-stack decision (F-09); (7) repo map and the edit rules - the previous README's source-grounding statement, status vocabulary and latest-slide note were preserved (header + section 6). Evidence: `00_README.md` structure verified (6 sections, 9 sub-items, no placeholder markers); cross-references point at existing repository files only.

## Currently In Progress

* Evidence baseline and this live progress record (this session, 2026-09-24).
* Increment 3 planning (Week 7 interface-freeze plan and Week 8-10 plan exist as documents): `project_sources/03_increment_3/WEEK_PLAN.md`.
* RF characterisation follow-up items that the repo's own documents mark as open: filling `docs/05_recommended_config.md`, bench smoke at the ICD configuration, and repeat/rail measurements.

* **Question routing document** (`docs/25_open_questions.md`): created 2026-10-10. All open questions routed by owner with options, recommended option and blocking flag. Owner sections: REII (Q-REI-001..20), MCTR-Lateral (Q-MCT-001..20), MCTR-Gearbox (Q-MCTG-001..20), EERI-Drive (Q-EEDI-001..20), EERI-Magnet (Q-EERM-001..15), Lecturer (Q-LEC-001..15). Evidence: this document itself.

* REII application-protocol proposal (`24_application_protocol_design_proposal.md`, 2026-09-25) - drafted, awaiting cross-package review. No loopback, no on-air application frame, no sign-off; every field value that is not sourced is tagged PROPOSED or TBD.

## Blocked / Waiting For

* **ICD Sec 4 payload freeze (D-01)** - blocked on MCTR + EEII agreement; `ICD_REII.pdf` Sec 4 "[ACTION REQUIRED]"; `Meeting Agenda_ Data Payload Agreements.pdf` "Finalize ICD Draft Version 1.2". A REII candidate map now exists (`24_application_protocol_design_proposal.md` Sec 10/11), so what remains is **agreement and sign-off**, not the absence of a proposal. R12-R16 (proposal Sec 19) list the specific answers owed by each package.
* **Protocol scales and units for hoist, load cell and magnet (R13/R15)** - MCTR-II SoW leaves the load-cell path (ADS1220 vs HX711) and the hoist envelope open; EEII-II owns the accepted magnet duty set and current thresholds. No repository source fixes these values, so they stay TBD in the proposal.
* **Command freshness / watchdog behaviour (Q10) and idle telemetry cadence (Q9)** - not defined in any repository document. Must be decided by the controller/safety owner, because they change post-link-loss behaviour; until then the proposal carries them as TBD.
* **IO-board owner + ESP IO-header SPI pins + magnet current-sense conditioning owner (D-06)** - `7.1 EngDes26 - Checkpoint Breakdown.pptx` slide 7: "IO board owner: EE2 or MC2 ... core/IO board schematics not yet finalised ... OPEN - RESOLVE BEFORE THE ICD IS DRAFTED"; ICD Sec 1 "Exact pins to be finalized based on EEII/MCTR usage".
* **Main control board hardware availability** - `EngDes26 - Checkpoint2.pptx` slides 3-4: board "In Production", "Completion estimate is 4 October", "DELIVERY ESTIMATE 9 OCTOBER 2026", "Boards arrive partially assembled. Connectors must be soldered by individual groups."
* **Controller Specification** (referenced by SysRS Sec 2.1 as an applicable document) is not present in this repository - pin-level detail cannot be verified from it.
* **Rail access slots** (Weeks 9-10) and group coexistence arrangement: `ACTIONS_NEEDED.md` "CONTACTS" and "F-06/F-07".
* **SysRS Sec 5.1 verification methods and PT3 disturbance threshold**: SysRS states both are pending a future revision.
* **Signed ICD**: no signed ICD file exists in this repository, although `DECISION_LOG.md` D-01 and `INTERFACE_MATRIX.md`/`I3_HANDOFF.md` assume "ICD_REII_v1.0_signed.pdf".

## Validation and Evidence Obtained

* 2026-10-15: generated visual-read artefacts `_sch_render/` (page1-5.png + J1/J3/J4/J6/U1
  zooms + `p2_words.txt` coordinate dump) â€” analysis artefacts, not original evidence; they
  back every VERIFIED-class claim in `docs/26`.
* 2026-10-15: `docs/26_controller_gpio_map.md` + `docs/27_reii_nrf_wiring_and_config.md`
  (IMPLEMENTED deliverables). Wiring/config content is **PROPOSED** â€” no test pass is claimed;
  bring-up evidence still owed per `docs/27` Â§5.
* 2026-10-15: measured build/boot logs (`EngDes26_start main rig micro/Port monitoring results
  after flash.txt`, `ESP_Build Terminal results.txt`, `esp_IDF_Size_report.txt`) â€” first
  MEASURED carrier evidence for the S3 board (I2C/ADS1220/SPWM/LED paths); SD timeout =
  no card fitted, not a board fault; **no RF evidence exists yet**.


Everything below was re-read from the stated source file during this audit. Values are quoted only where the file itself provides them.

### Bench RF measurements (only measured campaign in the repository)

| # | What was measured | Result (as recorded in the cited file) | Source path |
| --- | --- | --- | --- |
| 1 | Module bring-up on both test nodes | `radio.begin() OK`, `isPVariant=1`; all register read-backs match the firmware-written values; `PONG,1,3` control-link round trip; `BEACON`/`LISTEN` 233/233 with RPD=1 | `.../results/old/evidence/evidence/README.md` + the 5 verbatim `.log` files in the same folder |
| 2 | 1 m, PA_LOW, 3 rates x 2 modes, n=200 | NOACK 0.50 / 0.00 / 0.50 % (250 k / 1 M / 2 M); ACK 0.00 % on all three rates; channel 76 | `.../results/old/packet_loss_2026-09-05.csv` (6 rows, ch 76, PA LOW, n = 200); described in `docs/06_progress_summary.md` Sec 3.4 |
| 3 | Distance sweep 1-190 m LOS, ch 18, PA_MAX, n=1000/point | NOACK 250 k: 20.70 % (1 m) -> 10.30 % (190 m); NOACK 1 M: 23.80 % -> 80.00 %; NOACK 2 M: 0.00 % -> 38.10 %; ACK 0.00 % for all three rates at 1/2/5/10 m. 6 of the 45 ACK runs did not complete: 70 m 2 Mbps and 110 m 1 Mbps (`CTRL_TIMEOUT`); 150 m 2 Mbps and 170 m at all three rates (`TX_ABORTED`) | `.../results/packet_loss_2026-09-07.csv` (90 rows; channel 18, LOS = 1, n = 1000, PA = MAX throughout); earlier capture `.../results/old/packet_loss_2026-09-06.csv` (239 rows, distances 1-150 m) |
| 4 | 5 m channel interference benchmark, PA_MAX, n=1000 | ch 11 (inside Wi-Fi ch 2): NOACK 250 k 17.20 %, 1 M 20.10 %, 2 M 0.00 %; ch 4: 7.50 / 12.70 / 0.50 %; ch 2: 13.40 / 12.00 / 0.70 %; ch 6: 12.40 / 22.30 / 3.00 %; ACK 0.00 % on every row | `.../results/packet_loss_2026-09-08.csv` (24 rows); summarised in report Table 1 |
| 5 | Channel survey (RPD sweep 0-125 + Wi-Fi AP scan), 200 sweeps | 6 Wi-Fi APs seen on Wi-Fi ch 2/6/11; quietest channels scored 0.000 incl. ch 77; `RECOMMENDED: ch 77 (2477 MHz)`; `GROUP PLAN (5 links, >=4 ch apart): [4, 50, 73, 77, 81]` | `.../results/newest terminal channel analysis.txt`; `.../results/channel_survey_2026-09-08_1408.csv` (126 rows) and 8 sibling survey sets (09-07 x4, 09-08 x4 besides the cited set) + `.../results/old/channel_survey_2026-09-05/06` sets |
| 6 | Report-level consolidation | 1-190 m sweep, 1,000 packets, three rates; Table 1 (5 m benchmark incl. ch 11 17.20 / 20.10 %); Table 2 recommended config (ch 77, 2 Mbps, PA_MAX, ARC 5, ARD 500 us, 5 B / CRC-16); text: decoupling capacitors cured brownouts; antennas < 1 m at PA_MAX saturate the LNA | `.../REII327 nRF24L01 Link Bring-Up & Characterisation 40496325.docx` (Tables 1-2, Secs 2-6) and matching `.pdf` |
| 7 | Test-rig implementation | 6 PlatformIO environments (`rx_nodemcu32s`, `scanner_nodemcu32s`, `tx_nodemcu32s`, `tx_esp32c3`, `tx_esp32c3_uart`, `tx_nano`); 32-byte static payload, addresses `REII1`/`REII2`, control channel 80, ARC 15, ARD step 5 (= 1500 us at 250 kbps), SPI 4 MHz, per-board CE/CSN/SPI pin maps | `.../firmware/platformio.ini`, `.../firmware/lib/linktest/{protocol.h, radio_cfg.cpp, radio_cfg.h, board_pins.h, role_rx.cpp, role_tx.cpp, role_scanner.cpp}` |

### Not evidenced (no measured data in the repository)

* Rail / on-crane RF runs, NLOS runs, survey at the installed dongle position - `TEST_LOG.md` rail rows are blank and no `results/i3_rail/` data exists. `Evidence not available - status remains TBD.`
* PT1 (lateral >= 1 m/s at 1 m), PT2 (1.6 m in <= 20 s at 20 kg), PT3 (disturbance threshold undefined) - no measurement files. `Evidence not available - status remains TBD.`
* Dashboard fault-display timing, E-stop halt timing, sway magnitude, obstacle-planning runs - no logs/plots/video. `Evidence not available - status remains TBD.`
* Acceptance evidence for the RF report, the ICD, or the requirements baseline - no marking sheets, sign-offs, or approval records in the repository. `Evidence not available - status remains TBD.`
* Application-protocol evidence: the 32-byte budgets in `24_application_protocol_design_proposal.md` Sec 18 are paper arithmetic only - no `static_assert` build, no pack/unpack loopback across two ends, no application frame exchanged on air, no cross-package sign-off. `Evidence not available - status remains TBD.`
* Feature-level repeatability of the RF points: each distance/rate/mode combination has n = 1 burst of 1,000 packets (no repeats), so repeatability statistics are not available.

## Decisions Made

Recorded decisions are listed with their own declared status and the evidence path. Where the repository declares a status that the evidence does not yet support, that discrepancy is stated rather than resolved.

| ID (source) | Decision | Status as recorded | Evidence actually available | Assessment |
| --- | --- | --- | --- | --- |
| D-00a (`DECISION_LOG.md`) | Application RF baseline: RF_CH 77 (2477 MHz), 2 Mbps, PA_MAX, ARC 5, ARD 500 us, 5-byte address, CRC-16 | accepted | Report Table 2 (`REII327...docx` Sec 5.2); measured rows at 5/10 m in `results/packet_loss_2026-09-08.csv` / `packet_loss_2026-09-07.csv`; ICD Sec 2 states the same values as "shall be locked" | Values are consistently stated across report, ICD draft and DECISION_LOG. Interface-level ACCEPTED cannot be claimed: no signed ICD exists. Not yet implemented on any radio (test firmware runs a separate 250 kbps / ch 80 control config) |
| D-00b (`DECISION_LOG.md`) | No application-layer ARQ; hardware Auto-ACK + ACK-payload uplink telemetry | accepted | Report Sec 6; ICD Sec 3; measured ACK vs NOACK contrast (e.g. 5 m 250 kbps NOACK 5.60 % vs ACK 0.00 %) | Direction is documented and consistent; implementation on the final dongle/controller does not exist yet |
| D-00c (`DECISION_LOG.md`) | Separate robust control/test configuration: ch 80, 250 kbps, PA_MAX, ARC 15, ARD 1500 us | accepted | `protocol.h` (`CTRL_CHANNEL = 80`); `radio_cfg.cpp` (`RETRY_DELAY_STEPS = 5`, `RETRY_COUNT = 15`); bring-up log shows `Channel = 80`, `250 KBPS`, `ARD 1500 us`, `ARC 15` | Implemented in the test firmware (IMPLEMENTED). This is test/control configuration and must stay separate from the application baseline (`docs/04_channel_selection.md` explains the deliberate channel split) |
| D-00d (`DECISION_LOG.md`) | Wiring rules: pin tables generated, SPI <= 4 MHz on Dupont, 10 uF + 100 nF at module, antenna before power, PA_LOW under 2 m | accepted | `board_pins.h` (`NRF_SPI_HZ = 4000000`); `docs/01_hardware_selection.md`, `docs/02_wiring.md`; report Sec 2.2 (brownouts cured by decoupling); ICD Sec 1 (mandatory 10 uF + 100 nF, 3.3 V only) | Implemented for the test rig; mandatory-modification status carries to the final hardware via ICD Sec 1 |
| D-01 (`DECISION_LOG.md`) | Freeze 32-byte downlink/uplink byte maps (fixed structs + version byte) | "proposed -> accepted Week 7" | ICD Sec 4 is still TBD; no v1.2, no signed ICD, no loopback log in the repository | The acceptance claim is future-dated and not supported. Correct current status: NOT STARTED / not yet decided |
| D-02 (`DECISION_LOG.md`) | Telemetry via ACK-payload only | proposed | ICD Sec 3 direction; no rail CSV comparison yet | Consistent with ICD Sec 3; unproven at rail level |
| P-01 (REII proposal, 2026-09-25) | Candidate application protocol: fixed 32-byte little-endian structs, version `0x01`, downlink map per `24` Sec 10, uplink map per `24` Sec 11, 8-bit `seq`/`seq_ack`, hardware Auto-ACK only (no application ARQ) | PROPOSED (not a decision) | `24_application_protocol_design_proposal.md` Sec 10-14, 18 - paper design; no loopback, no build, no sign-off | Input to the D-01 workshop only. D-01 remains undecided and nothing is frozen; the proposal explicitly defers scales, enums and freshness to MCTR/EEII (R12-R16) |
| P-02 (REII proposal, 2026-09-25) | Nine trade-study recommendations with weights C1-C9; choices 2A (one segment per frame), 7C (`error_code` + `fault_flags`) and 8A (fixed struct + version byte) flagged marginal | PROPOSED (not a decision) | `24_application_protocol_design_proposal.md` Sec 7-9 | Alternatives documented as required by the course process artefacts; marginal choices must be re-checked if MCTR/EEII push back |
| D-03 (`DECISION_LOG.md`) | Rate locked at 2 Mbps in the operational envelope | proposed | Report Sec 3.3 recommends 2 Mbps at 5-10 m; long-range rows show 2 Mbps NOACK degradation and ACK timeouts beyond ~70 m | Proposed; rail NLOS data absent |
| D-04 (`DECISION_LOG.md`) | Sway shaping: S-curve baseline, ZV shaper if frequency identified | proposed | SoW item 4 requires the modelling; no model file or sway measurement exists | Proposed only; no engineering artefact yet |
| D-05 (`DECISION_LOG.md`) | Dongle bring-up order: CDC echo -> SPI -> RF | proposed | Plan text only (`WEEK_PLAN.md` Week 8) | Proposed; no STM32 project exists |
| D-06 (`DECISION_LOG.md`) | IO-board owner + ESP IO-header SPI pins + current-sense conditioning owner | proposed (target Week 7) | Slide 7 lists all three as OPEN; ICD Sec 1 pins TBD | Open; blocks the ICD freeze |
| D-07 (`DECISION_LOG.md`) | Rail RF re-survey at dongle position; channel guard >= 4 | proposed (target Week 9) | Bench survey only; report Sec 4.2 states the >= 4-channel guard requirement; tool proposed plan `[4, 50, 73, 77, 81]` | Open; bench-only evidence |

Other recorded proposals (not yet accepted):

* Payload struct options (ramp-rates vs waypoints; vectors vs trajectory download; boolean vs PWM magnet; bitmask vs separate bytes; absolute vs tick positions; raw vs scaled units; single code vs fault bitmask) - listed with pros/cons in `Meeting Agenda_ Data Payload Agreements.pdf`; no selection is recorded in the repository.
* Open decisions tracked in `ACTIONS_NEEDED.md` (C0 old-numbers correction; D-01..D-06; F-01..F-09 facts to fetch; V-01..V-03 wording questions).

## Contradictions and Inconsistencies (reported, not silently resolved)

1. **Legacy numeric requirement set vs SysRS.** `project_sources/00_requirements/general_requirements.txt` states 2000 kg payload, 5 m clearance, 0.5 m/s hoist, 1.5x holding force in 2 s, 5-degree sway limit, 1.5 s e-stop stop time, IP54, fail-safe brake - with no author/organisation/date header (no authority until attributed). `EngDes26 - SysRS v1.0.pdf` (PHY1, PT1-PT3, Sec 4.6-4.8) instead fixes 20 kg on a 600x400x3 mm plate, 1.6 m hoist stroke in <= 20 s, lateral >= 1 m/s at 1 m, PT3 threshold [TBD], and contains no IP54 / 5-degree / 50 ms / 1.5 s binding values. `ACTIONS_NEEDED.md` C0 already flags this. Authority: SysRS is the primary system requirements authority and the five SoWs are authoritative for their work-package scope; the legacy numbers must not be presented as requirements unless a clause in one of those documents is cited.
2. **50 ms / 1.5 s / 5-degree gates used in planning artefacts.** `INTERFACE_MATRIX.md` and `TEST_LOG.md` carry "<= 50 ms" fault display, "<= 1.5 s" E-stop halt and "<= 5 deg" sway as verification rows; `docs/05_recommended_config.md` reasons about a "50 ms requirement"; `What Exactly You Need to Start Doin.txt` gives 50 ms as a template *example*. The SysRS contains none of these values (FR12/FR13 have no timing; FR6 "without drift" has no seconds). These are internal REII targets, not normative requirements, until a cited source establishes them (`ACTIONS_NEEDED.md` V-01).
3. **Two different "no app ARQ" vs "handshake + sequence validation" designs.** ICD Sec 3 / report Sec 6 state that no application-layer ARQ is needed (hardware Auto-ACK only), while `System_FFBD.drawio` proposes 3.2.3 "Transmit link-request packet", 3.2.4 "Receive link acknowledgement", 3.2.5 "Verify protocol version and controller identity", 3.4.1.3 "Validate frame integrity and sequence number" and 3.4.7.4 "Confirm the frame acknowledgement" for telemetry. The Draw.io model implies version/sequence/handshake elements that are not defined in the ICD draft. Reported as a design-model vs interface-document mismatch. **Proposed resolution (not agreed):** `24_application_protocol_design_proposal.md` Sec 14 separates the layers - L1 hardware Auto-ACK and L2 ACK-payload transport (FACT), L3 startup existence/version synchronisation (PROPOSED), L4 sequence validation and duplicate/stale rejection (PROPOSED), L5 command freshness/watchdog (TBD, safety owner), L6 hardware e-stop (FACT) - and argues that none of L3-L5 is application-level ARQ. Requires the controller owner's approval (V-02).
4. **Recommended-config deliverable still blank while the report states final values.** `docs/05_recommended_config.md` has placeholder `___` entries in every "Measured evidence" and "Decision" cell, and its latency budget check is unfilled; the report DOCX Table 2 and ICD Sec 2 nevertheless state the final values. The measured justification table therefore remains incomplete.
5. **ESP32-S3 module variant discrepancy.** `project_sources/00_requirements/esp32-s3-dev-board-overview.pdf` (page 1) describes the module as "ESP32-S3-WROOM-1U module - 16MB flash, 8MB PSRAM", while SysRS DR5 and `7.1 EngDes26 - Checkpoint Breakdown.pptx` slide 7 specify "ESP32-S3-WROOM-1 ... N8R2" (8 MB flash / 2 MB PSRAM, and no external-antenna 'U' suffix in the SysRS wording). Both refer to the supplied controller board; the exact module variant needs confirmation.
6. **Mirror provenance vs. local git history.** `22_latest_slide_sources.md` cites commit `04e8703bb0740c7be88efd983138042f92b38134` dated 2026-09-23 for the two newest decks, and `results/newest terminal channel analysis.txt` refers to `D:\DESIGN GIT\Design-Git-AI\...` paths; the local repository history contains only `9c406b0` (initial) and `7bc09fc` ("new"). The evidence files are present, but the cited commit hash cannot be verified in this clone.
7. **Acceptance claims ahead of evidence.** `DECISION_LOG.md` D-01 records "proposed -> accepted Week 7" and `I3_HANDOFF.md` expects `Increment 3/ICD_REII_v1.0_signed.pdf`; neither a signed ICD nor a Week-7 sign-off artefact exists in the repository.
8. **ARC 5 (ICD baseline) vs ARC 15 (characterisation recommendation).** `ICD_REII` Sec 3 and the report Table 2 state `ARC = 5`, while `docs/05_recommended_config.md` records "15 during characterisation; product value = smallest count that gives >= 99.9 % delivery at max lab distance, capped so worst-case latency <= 50 ms". The repository never selects the normative value. REII's proposal treats 5 as the interface baseline and 15 as test configuration (`24_application_protocol_design_proposal.md` Sec 14); this must be settled before the ICD is signed.
9. **ESP32-S3 board / header description conflict.** `ICD_REII` Sec 1 says the radio connects via the "ESP IO header"; `7.1 EngDes26 - Checkpoint Breakdown.pptx` contains a controller pin budget and the open item "IO board owner: EE2 or MC2 ... OPEN - RESOLVE BEFORE THE ICD IS DRAFTED" (slide 7); the ESP32-S3 dev-board overview describes the WROOM-1U module while SysRS DR5 / the same deck wording says WROOM-1 N8R2 (see item 5). Which header and which module populate the final controller is unconfirmed (D-06), so the proposal deliberately fixes no pins.

## Outstanding Actions

1. Resolve the requirements dispute (item 1 above): adopt SysRS values and strike legacy numbers, or cite the authority for them (`ACTIONS_NEEDED.md` C0).
2. Decide the fate of the 50 ms / 1.5 s / 5-degree internal targets (keep as internal REII targets with owner, or delete) - V-01.
3. Run the payload byte-map workshop with MCTR/EEII and either freeze ICD Sec 4 or record a PROVISIONAL map with owner and revisit date; publish a signed ICD (D-01).
4. Obtain the Controller Specification and the ESP32-S3 controller source; confirm ESP IO-header SPI pins and IO-board ownership (D-06, F-01) â€” partial input now available: verified pin table + candidate attach header **J1** and proposed nRF pin set (`docs/26`, `docs/27`); still missing: Controller Spec, rig application source, and the cross-package allocation / IO-board-owner decision.
5. Fill `docs/05_recommended_config.md` measured/decision columns from the existing measured data; add repeat runs at the operational distances.
6. Bench smoke test at the ICD application configuration (ch 77 / 2 Mbps / ARC 5 / ARD 500 us) with the existing test firmware before the dongle build.
7. Start the STM32F411 dongle bring-up in the agreed order (CDC echo -> SPI -> radio) once the ICD link layer is signed (D-05).
8. Plan and execute the rail campaign (survey at dongle position, 5/10 m LOS + NLOS rows, fault-display and E-stop timing captures, sway flat-vs-shaped runs) - `WEEK_PLAN.md` Weeks 9-10.
9. Reconcile `INTERFACE_MATRIX.md` rows to the final requirement set before they are used as pass/fail gates.
10. Record the decision outcomes of the payload workshop in `DECISION_LOG.md` with date, owner, and evidence.
11. Circulate `24_application_protocol_design_proposal.md` to MCTR-I, MCTR-II, EEII-I, EEII-II and the controller owner; collect answers to R12-R16 (proposal Sec 19).
12. After agreement, freeze ICD Sec 4 (or mark it PROVISIONAL with owner + revisit date), then build the pack/unpack unit test with `static_assert(sizeof == 32)` on both ends and run an on-target loopback before any rail command exchange.
13. Confirm or reject the proposal's interpretation of the FFBD "handshake" (proposal Sec 14, layer L3) and settle the ARC 5 vs ARC 15 conflict (Contradictions item 8).

## Risks / Dependencies

* **Cross-package dependencies:** ICD payload content depends on MCTR I/II and EEII I/II; dongle RF work depends on the frozen link configuration; rail work depends on MCTR/EEII rail availability and slots (Weeks 9-10).
* **Hardware availability:** the shared controller board is in production with a 9 October 2026 delivery estimate and partial assembly required by groups (`EngDes26 - Checkpoint2.pptx` slides 3-4). Bench work with the ESP32-C3 stand-in is possible; controller-side integration is time-boxed by this.
* **Link margin at range:** the 1-190 m sweep shows NOACK PER degrading at the far end (1 Mbps to 80.00 %, 2 Mbps to 38.10 % at 190 m) and 6 of 45 ACK runs failing to complete beyond 70 m, so NLOS/rail measurements could invalidate the "locked 2 Mbps" assumption (D-03). The 250 kbps robust control channel exists partly for this reason.
* **Interference:** campus Wi-Fi occupancy changes and other groups' links; the >= 4-channel guard is a proposal plus a tool plan, not an agreed inter-group allocation.
* **Safety boundary:** PHY2 (hardware e-stop removing HV) and PHY3 (RCD) are EEII-I/system-level responsibilities; the dashboard E-stop flag must not be treated as the safety stop (V-03).
* **Interface protection:** slide 7 requires bench verification of opto-isolation before live gate drivers; no isolation test log exists yet.
* **Evidence gaps that could invalidate planning:** no signed ICD, no Controller Specification, no rail data, no acceptance records.
* **Repository hygiene:** `results/old/evidence/evidence/` is double-nested, and the RF sub-project exists twice (root of `rf_characterisation/` and `submission/`); confirm which copy is the graded artefact before quoting figure numbers.
* **Document-accessibility limitation (audit tooling):** `project_sources/03_increment_3/Actions needed.pdf` contains vector-outline text only (0 font objects, no text operators in any stream), so it could not be machine-read with the tools available (pypdf / python-docx / python-pptx / xmllint-style parsing; no OCR or PDF-rendering library installed). The sibling `project_sources/03_increment_3/ACTIONS_NEEDED.md` was used as the Increment 3 planning artefact; equivalence between the PDF and the Markdown is unverified. No new packages were installed in this session.

## Milestone Completion Criteria

| Milestone | Evidence required before VALIDATED / ACCEPTED |
| --- | --- |
| Requirements baseline | SysRS revision containing Sec 5.1 verification methods and the PT3 threshold, with the legacy-number conflict closed and a statement of which documents are binding |
| Architecture models | Models consistent with the signed ICD interface set, exported views reviewed, and traceability to SysRS interfaces stated |
| ICD | Sec 4 byte maps frozen (or PROVISIONAL with owner/date), ESP IO-header pins fixed, signatures recorded in a committed signed PDF |
| RF characterisation | Repeat runs confirming the quoted PER values, ICD-configuration smoke results, filled `docs/05_recommended_config.md`, and rail-position survey |
| RF report | Marking/acceptance record, or an explicit statement that acceptance evidence is unavailable |
| STM32F411 dongle | Reproducible build, CDC echo evidence, SPI register read-back matching the ICD values, USB-to-RF bridge demonstrated, and a committed evidence log |
| ESP32-S3 integration | Committed controller-side implementation, radio init read-back, RX mode, ACK-payload telemetry demonstrated, and a test log |
| Application protocol | Byte-exact definition plus pack/unpack loopback evidence across both ends, and successful command/telemetry exchange on the rail |
| Dashboard | Telemetry display for all FR12 parameters, command set for FR13, fault display path exercised with a timestamped capture |
| Anti-sway | Model plus derived shaping parameters documented, and measured sway on the rail with the shaped vs baseline comparison |
| Obstacle avoidance | Documented approach and map source, plus a demonstrated commanded path around a known obstacle |
| Rail integration | All `INTERFACE_MATRIX.md` rows pass with committed CSVs/logs, or NCRs are raised with owners and due dates |
| Safety interface | Isolation bench-verification log and confirmation of the PHY2 hardware e-stop path with evidence that it is independent of dashboard/software |
| I3 handoff | All path entries in `I3_HANDOFF.md` filled with committed artefacts and signed off by the REII lead and MCTR/EEII counterparts |
| Course process artefacts | Documented alternative analysis with criteria, scores and justification where a genuine design choice exists |

## Milestone Detail (objective, requirement, state, evidence, actions)

### 1. Requirements baseline

* Objective: establish the binding requirement set for the REII work package and the interfaces it must satisfy.
* Requirement source: `EngDes26 - SysRS v1.0.pdf` (normative), `EngDes26_SoW_REII_Remote_Control_Dashboard.pdf` (REII scope), 4 sibling SoWs for interface context.
* Status: IN PROGRESS.
* Implementation state: document set retrieved and verified; no derived REII requirement specification artefact exists.
* Testing state: not applicable (document-level).
* Validation state: cannot be validated - SysRS is Rev 0 "for discussion" with Sec 5.1 verification methods deferred and PT3 open.
* Decisions: none recorded at REII level for the requirement set.
* Blockers: SysRS revision; legacy-number dispute (see Contradictions item 1).
* Outstanding actions: confirm the binding set for I3; close the legacy-number dispute; record internal targets (if any) with owner.
* Risks: continuing to plan against legacy numbers would produce work that satisfies no normative requirement.
* Completion criteria: as in the completion-criteria table (SysRS revision + conflict closure).

### 2. System architecture models

* Objective: maintain system and functional models of the hoisting system and the REII subsystem consistent with the interface set.
* Requirement source: SysRS Sec 4.1/4.5 interface lists; SoW item 3-5.
* Status: IN PROGRESS.
* Implementation state: `System_FFBD.drawio` contains 65 numbered functions incl. the REII branches (3.2 connect/nRF configuration/link handshake/state sync; 3.3 operator command -> encoding -> USB transfer -> RF transmit -> acknowledgement; 3.5 telemetry receive/transfer/decode/display/fault alert/log); `Sys_Architecture.drawio` contains the F/U 6.0 Remote Controller decomposition (6.1 dashboard application, 6.2 ground-station dongle MCU, 6.3 nRF24L01 module, 6.4 antenna, 6.5 USB bus power) plus I/F C.1-C.5.
* Testing state: not applicable.
* Validation state: models not yet reconciled with the ICD draft; no acceptance evidence.
* Decisions: none recorded.
* Blockers: ICD freeze (pin/interface names) and the ICC/cross-package interface numbering.
* Outstanding actions: align models to the signed ICD; export review images; reconcile with SysRS Sec 4.1/4.5.
* Risks: the FFBD's implied handshake/sequence scheme conflicts with the ICD's "no app ARQ" position (Contradictions item 3).
* Completion criteria: models consistent with the signed ICD and traceable to SysRS interfaces.

### 3. Interface Control Document (ICD_REII)

* Objective: freeze the physical, data-link and application-layer interfaces between the REII subsystem and the shared controller.
* Requirement source: SysRS IR-list/Sec 4.1 (Controller <-> REII = "Wireless nRF24L01 link ... comms protocol is REII's own design"); SoW item 2.
* Status: IN PROGRESS.
* Implementation state: draft DOCX/PDF with Sec 1 (hardware/physical), Sec 2 (data-link configuration), Sec 3 (application-layer protocol direction) written; Sec 4 (32-byte payload structures) explicitly TBD pending MCTR/EEII agreement.
* Testing state: not applicable; the ICD values have not been exercised as an interface on shared hardware.
* Validation state: no signed/approved ICD; no acceptance evidence.
* Decisions: D-00a/b accepted at sub-project level; D-01 open (see Decisions Made).
* Blockers: MCTR/EEII payload agreement; IO-board owner and ESP IO-header SPI pins.
* Outstanding actions: hold the payload workshop, freeze or PROVISIONALLY map Sec 4, fix the pins, sign and commit.
* Risks: implementing firmware against an unfrozen Sec 4 would freeze a design by accident; silent ICD changes are prohibited by the rules.
* Completion criteria: signed ICD with Sec 4 frozen or explicitly provisional (owner + revisit date).

### 4. RF characterisation (bench) and its report

* Objective: prove the nRF24L01 link is reliable before committing the dongle build and the application protocol (stated objective in `README.md` and the report abstract).
* Requirement source: course checkpoint task (`7.1 EngDes26 - Checkpoint Breakdown.pptx` slide 4: live link on a dev board; packet loss vs distance at >= 2 rates; best channel; recommended configuration); SysRS DR4 (nRF24L01 mandated).
* Status: TESTING (campaign) / IMPLEMENTED (report artefact).
* Implementation state: 6 PlatformIO environments; role dispatch (RX/TX/scanner); 32-byte static payload; serial command interface; Python tooling (`run_test.py`, `plot_packet_loss.py`, `channel_analysis.py`, `gen_wiring_drawio.py`).
* Testing state: measured - bring-up logs (2026-09-05), 1 m PA_LOW matrix (n=200), 1-190 m LOS sweep at ch 18 (n=1000/point), 5 m multi-channel benchmark (n=1000/point), 200-sweep channel surveys on four days.
* Validation state: not validated as an interface - the ICD application configuration (ch 77 / 2 Mbps / ARC 5 / ARD 500 us) has not been run by the firmware in the repository (the firmware's control channel is 80 at 250 kbps; the test matrix used ch 18), no repeat runs, and `docs/05_recommended_config.md` is unfinished. `Evidence not available - status remains TBD.`
* Decisions: D-00a/b/c/d (see Decisions Made); proposal `[4, 50, 73, 77, 81]` for five-link coexistence.
* Blockers: hardware availability for repeat/rail runs; other groups' activity for the coexistence survey.
* Outstanding actions: repeat key points; run the ICD configuration smoke test; complete `docs/05`; rail survey.
* Risks: PA_MAX saturation under 1 m; brownouts if decoupling is omitted; the measured LNA/short-range and long-range behaviours differ strongly per rate, so extrapolation to rail conditions is unsafe without measurement.
* Completion criteria: repeatable PER/retry data at the operational distances plus the ICD-configuration smoke test.

### 5. STM32F411 ground-station dongle

* Objective: build the ground station that bridges the PC dashboard to the nRF24L01 link (`ICD_REII` Sec 1; SoW item 1).
* Requirement source: SoW item 1 ("building the STM32F411 ground-station dongle from scratch - native USB, nRF24 driver, USB bridging, with no scaffolding provided"); ICD Sec 1; SysRS Sec 4.1.
* Status: NOT STARTED.
* Implementation state: nothing in the repository; no STM32 project, no `*.ioc`, no HAL/TinyUSB sources.
* Testing state: none.
* Validation state: none. `Evidence not available - status remains TBD.`
* Decisions: D-05 proposed (CDC echo -> SPI -> RF order).
* Blockers: ICD link-layer sign-off; hardware procurement/build time; Week-8 plan slot.
* Outstanding actions: create the project, CDC echo, SPI bring-up, radio init read-back matching ICD Sec 2, bridge function.
* Risks: copying characterisation pin maps into the STM32 design would be unsupported (the test nodes use NodeMCU-32S / ESP32-C3 pins); SPI timing on breadboard limited to 4 MHz.
* Completion criteria: as in the completion-criteria table.

### 6. ESP32-S3 controller RF integration

* Objective: complete the controller-side nRF24 integration (RX side of the link, ACK-payload telemetry source).
* Requirement source: SoW item 1 ("completing the stubbed nRF24 SPI driver on the ESP32 controller side"); ICD Sec 1 (ESP IO header) and Sec 3 (uplink via ACK-payload).
* Status: NOT STARTED.
* Implementation state: no controller source in the repository; the Controller Specification referenced by SysRS Sec 2.1 is absent; only ESP32-C3 test-node firmware exists.
* Testing state: none.
* Validation state: none. `Evidence not available - status remains TBD.`
* Decisions: none recorded for the controller side.
* Blockers: Controller Specification; IO-board schematics; delivery of the shared controller board (9 October 2026 estimate per `EngDes26 - Checkpoint2.pptx`).
* Outstanding actions: obtain the spec/source, implement the stub driver, RX mode, ACK-payload telemetry, then verify on the shared controller.
* Risks: ESP32-C3 pin maps and test firmware must not be assumed to carry over (explicitly warned in `13_esp32_controller_integration.md` and ICD).
* Completion criteria: as in the completion-criteria table.

### 7. Application protocol (32-byte payloads)

* Objective: define the downlink command and uplink telemetry structures that fit the nRF24 32-byte limit.
* Requirement source: SoW item 2 (framing, addressing, reliability mechanism = open design space); ICD Sec 3/4; SysRS FR12/FR13 telemetry and control content.
* Status: IN PROGRESS (proposal drafted 2026-09-25; cannot be frozen unilaterally and remains subject to MCTR/EEII agreement on ICD Sec 4).
* Implementation state: the test rig uses a private 32-byte test protocol (`protocol.h`: CmdFrame / DataFrame with `seq` / ReportFrame, size-checked with `static_assert`) that is not the application protocol. `24_application_protocol_design_proposal.md` now defines candidate application structures: fixed 32-byte little-endian downlink map (Sec 10) and uplink map (Sec 11), with 8/3 reserved bytes for I4 growth; no application firmware exists on either end.
* Testing state: none for the application protocol (test-rig frames only). The 32-byte budgets (proposal Sec 18) are paper arithmetic - not a build and not a measurement.
* Validation state: none. `Evidence not available - status remains TBD.` No `static_assert` build, no pack/unpack loopback, no rail exchange, no cross-package sign-off.
* Decisions: D-01 open; option set documented in the meeting agenda without selection. Proposal-level recommendations only (9 pairs, proposal Sec 9), each tagged PROPOSED, with 2A/7C/8A flagged marginal.
* Blockers: MCTR/EEII agreement; R12-R16 answers (rail datum and encoder resolution, hoist envelope, load-cell path, current-sense scaling, magnet duty set, error-code table, freshness/watchdog).
* Outstanding actions: circulate the proposal; hold the workshop; record the decision; freeze or PROVISIONAL-mark Sec 4; then the byte-exact loopback test.
* Risks: a late Sec 4 freeze would invalidate dashboard and firmware work; a PROVISIONAL map must carry an owner and revisit date; scales chosen without MCTR/EEII confirmation (current sense, load cell) could be wrong by a factor and force a v0x02 or a documented rescale.
* Completion criteria: byte-exact definition plus loopback evidence both ends.

### 8. Dashboard software (FR12/FR13)

* Objective: operator telemetry display and full command/control.
* Requirement source: SysRS FR12 (display position, speed, load, current, fault, grip status), FR13 (control motion, hoist, magnet), OQ3 (usability); SoW item 3.
* Status: IMPLEMENTED.
* Implementation state: `project_sources/03_increment_3/dashboard/` â€” Flask + SocketIO backend (`app.py`), 32-byte protocol pack/unpack (`protocol.py`, PROVISIONAL ICD Â§4 maps per D-01), USB-CDC serial reader (`serial_reader.py`), vanilla HTML/JS frontend (`templates/index.html`, `static/app.js`, `static/style.css`), `requirements.txt`. DECISION_LOG.md D-10 marked TESTING (firmware flash + live serial COM12 test pending) (stack A1 Python web). INTERFACE_MATRIX.md created documenting all interfaces.
* Testing state: none against live hardware; unit-level pack/unpack assertions pass at import. Requires live COM12 serial connection to groundstation for end-to-end test.
* Validation state: not yet validated â€” awaiting live serial test against groundstation COM12.
* Decisions: D-10 (A1 Python web stack) recorded in DECISION_LOG.md as TESTING (firmware flash + live serial COM12 test pending).
* Blockers: ICD Sec 4 freeze (D-01 still PROPOSED â€” byte maps are PROVISIONAL); live groundstation serial port (COM12) not yet connected for testing.
* Outstanding actions: connect groundstation to COM12, run dashboard, verify telemetry display + command forwarding; validate pack/unpack against firmware loopback.
* Risks: ICD Â§4 byte maps are PROVISIONAL â€” if MCTR/EEII change field offsets/scales, protocol.py must be updated. Internal 50 ms fault-display target is not a SysRS requirement (Contradictions item 2) - record as internal if used.
* Completion criteria: FR12/FR13 demonstrated with captures from live groundstation serial.

### 9. Anti-sway and obstacle avoidance (SoW items 4-5)

* Objective: model payload/pendulum dynamics, derive open-loop feedforward profiles, and plan paths around known obstacles.
* Requirement source: SoW items 4-5; SysRS FR1/FR2 (lateral motion + encoder feedback for profiling).
* Status: NOT STARTED.
* Implementation state: none; no model, no generator, no obstacle map source.
* Testing state: none.
* Validation state: none. `Evidence not available - status remains TBD.`
* Decisions: D-04 proposed (S-curve baseline, ZV if frequency identified).
* Blockers: MCTR-I drive command format (D-04 in `ACTIONS_NEEDED.md`), encoder resolution confirmation, rail access.
* Outstanding actions: model, parameter derivation, generator, rail measurement of sway; document obstacle-map source.
* Risks: without identified sway frequency the shaping result cannot be substantiated; no sway measurement exists to bound performance.
* Completion criteria: model + measured rail comparison; demonstrated path around a known obstacle.

### 10. Rail integration, safety and handoff

* Objective: demonstrate the integrated REII subsystem on the crane rail and hand over to I4.
* Requirement source: SoW item 6 (validation against actual crane operation); SysRS PHY2/PHY3; `INTERFACE_MATRIX.md` rows.
* Status: NOT STARTED.
* Implementation state: plans only (`WEEK_PLAN.md` Weeks 9-10; `I3_HANDOFF.md` with "TBD fill" paths).
* Testing state: no rail measurements, no fault-timing captures, no sway plots, no isolation log, no E-stop timing evidence.
* Validation state: none. `Evidence not available - status remains TBD.`
* Decisions: D-06, D-07 proposed.
* Blockers: rail slots; controller board delivery; MCTR/EEII availability; safety isolation verification.
* Outstanding actions: execute the rail campaign per `WEEK_PLAN.md`; fill `TEST_LOG.md`; raise NCRs; complete the handoff pack.
* Risks: verifying interfaces on the rail without a frozen ICD would invalidate the test evidence; dashboard E-stop must not be confused with the PHY2 hardware e-stop.
* Completion criteria: matrix pass or NCRs with owners; handoff pack complete and signed.

<!-- END -->

