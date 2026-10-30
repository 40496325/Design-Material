# REII Crane Dashboard

The operator's PC application for the Group 13 gantry crane: **live monitoring of every telemetry field and full command of the crane** over the nRF24 radio link (SysRS FR12, FR13, OQ3; REII SoW items 3–5).

It has two parts:
* **Backend (`server.py`), run in a terminal.** It owns the ground station's USB port and runs the ICD IF10.0 operating sequence: USB check → radio set-up → 20 Hz command loop.
* **Web page (`web/`), opened in a browser** at `http://localhost:5000`. It works offline; nothing is loaded from the internet.

```
 Browser page ──HTTP / SSE──► server.py ──USB 115 200──► ground station ──nRF24 ch77 2 Mbps──► crane MCU
 (controls, drawing,           (20 Hz Table 33 loop,       (NodeMCU-32S, COM10)    ◄── Table 34 telemetry
  readouts, log)                ramp, deadman, CSV)                                    in every auto-ACK
                     optional ──USB──► crane console (COM12): load cross-check, tare / calibrate, PA / channel
```

---

## Build progress

| Date | State | Where |
|---|---|---|
| 18–20 Oct | **First dashboard** (Increment 3): Flask + Socket.IO, Table 33 / 34 pack and unpack, sliders, fault list. Never saw an ACK, because the radio link itself was down (0 frames on the crane), so it was never verified | `Increment 3/dashboard_Pycharm_firmware/` (superseded) |
| 28 Oct | **Rewritten for Increment 4.** Python standard library HTTP server + Server-Sent Events, so the only dependency is `pyserial`. Added the ICD operating sequence, 20 Hz control loop, full Table 34 decode, gantry drawing, S-curve ramp, deadman, soft stop, per-command CSV and event log | this folder |
| 28 Oct ~22:48 | **Live load cell.** Real load in the readout, the bar and the chart. USB cross-check against the crane's own reading. **Tare** / **Calibrate** buttons | snapshot: `../Backup Dashboard with load cell working/` |
| 28 Oct ~23:23 | **Radio panel.** Power LOW / HIGH on both ends. Clean-channel switching (75–81, 2–4) with automatic fall-back. The crane is put back on the dashboard's channel after a reboot | snapshot: `../Backup Channel swithcing  added plus RP/` |
| 29 Oct 00:00 | Verified: link up, crane resync after reboot, **channel 77 → 80 → 77 switched and confirmed** with the crane answering on each | `../results/dashboard/20261029_000031/` |

**Next** (B1–B6 in `../README.md` → *Dashboard*):
* background-tab heartbeat fix;
* measured / modelled badges;
* PA MAX in the UI;
* **auto mode + obstacle planner**;
* **ZV anti-sway**;
* session summary.

---

## Features

### Monitoring (FR12)

| Area | What it shows |
|---|---|
| **Gantry drawing** | Trolley position along the rail, hook height, magnet on / off, the load box with its weight, rail end-stops (red at a rail limit), direction arrows |
| **Readouts** | Every ICD Table 34 field in engineering units: trolley position (m) / speed (m/s), hook height (m), hoist speed (mm/s), load (kg) with a 20 kg bar, hoist-drive current (A), magnet current (A) + status, telemetry age |
| **Faults** | The 8 `fault_flags` lamps (VFD overcurrent, overload > 20 kg, brake, rail limit, magnet, encoder, VFD fault, wireless timeout) and `error_code` |
| **Crane status** | The 5 `system_health` lamps: link OK, E-stop, auto mode, brake clamped, VFD enabled |
| **Link bar** | State, loss over the last 100 commands, round-trip time, retries, commands sent, radio power and channel |
| **Last 30 s chart** | Trolley position, hook height, load |
| **Last frames on air** | The 32 bytes of the last command (Table 33) and the last reply (Table 34), coloured by field; hover a byte for its name |
| **Event log** | Link up / lost, faults raised / cleared, deadman, soft stop, channel / power changes, crane warnings |

### Control (FR13)

| Control | Effect (Table 33) |
|---|---|
| **Trolley / Hoist sliders** (−100…+100 %) | Target speed. The value actually sent ("Sent to crane") follows it through the **S-curve ramp** (100 %/s acceleration, 400 %/s² jerk). `segment_idx` counts ramp steps |
| **Return to zero when released** (on by default) | Slider springs back to 0 when let go |
| **Keyboard** | ← → trolley, ↑ ↓ hoist (60 % while held), **M** magnet, **Space** soft stop |
| **Energise magnet** | byte 6 `magnet_state` |
| **Hold hoist brake** / **Auto mode** | byte 3 bits b4 / b2 |
| **Reset faults** / **Zero position** | b1 / b3, sent for 5 commands |
| **Ping crane** | one `msg_type` 1 frame |
| **Soft stop** | b0 + both speeds to 0 at once; latched until **Release soft stop** |

**Soft stop is a software command.** The red hardware E-stop (SysRS PHY2) is the safety stop and cuts HV on its own.

### Radio panel

* **Power: Low (−12 dBm) / High (−6 dBm).** Changed on the ground station, and on the crane too when `--rig-port` is given. MIN / MAX come from the command line (`--pa MAX` for the rail).
* **Channel: the clean channels from the Increment 2 surveys.** 75–81 (above Wi-Fi ch 11; 77 is the ICD default) and 2–4 (below Wi-Fi ch 1). Hover for the worst measured occupancy; the list is in `radio.py`.
  * Needs `--rig-port`; the trolley and hoist must be at rest.
  * The crane switches first, then the ground station.
  * If no ACK arrives within 6 commands (0.3 s), both ends go back to the previous channel by themselves.
  * On Ctrl+C the crane is put back on 77.

### Load cell (with `--rig-port`)

* The big number is the **radio value** (Table 34 bytes 10–11, 20 times a second).
* The green line under it is the **crane's own reading over USB** (`LOAD WATCH`, 5 Hz). "Radio value matches" means the two agree to within 0.15 kg.
* **Tare** (hook empty).
* **Calibrate:** hang a known mass, type its kg, press Calibrate. The event log shows *load cal done*.

### Safety built in

* **Deadman.** If the page's heartbeat stops for 1 s (tab closed, PC asleep), both targets go to 0 and the ramp stops the crane. A banner is shown.
* **Lost link.** After 10 missed ACKs in a row (~0.5 s) the link shows **No reply from crane**. The crane's own 500 ms timer puts it in **Safe Hold** (speeds 0, brake clamped, magnet kept on; ICD D8) and latches *Wireless timeout* until **Reset faults**.
* **Dashboard closed.** Commands stop, so the crane enters Safe Hold by itself.

---

## Setup

**Needs:** Windows PC, Python ≥ 3.10, the ground station on COM10 (and optionally the crane's USB on COM12), a browser.

1. **Flash both boards** with the Increment 4 firmware: crane `crane_rig_IDF` v4.4.1, ground station `groundstation_PIO` v4.1.0 (see `../README.md`).
2. **Check the link** with the smoke test (`../tools/smoke_test.py`). The dashboard assumes a working link.
3. **Close every serial monitor.** Windows lets only one program hold a COM port.
4. Install the one dependency:

   ```powershell
   cd "D:\DESIGN GIT\Design-Material\Increment 4\dashboard"
   python -m pip install -r requirements.txt
   ```

## Usage

```powershell
python server.py --port COM10                          # ground station only
python server.py --port COM10 --rig-port COM12 --open  # + crane console: load cross-check, tare / calibrate, channel switching
python server.py --port COM10 --rig-port COM12 --pa MAX --open   # on the rail (5-10 m)
```

Open **http://localhost:5000** (`--open` does it for you). Stop with **Ctrl+C**.

On start the terminal and event log show the operating sequence:

```
Link connecting: checking ground station on COM10
Ground station ready: ch 77, 2Mbps, PA LOW, profile ICD
Link running
```

A *Wireless timeout* fault can appear right after start. If the crane was already running before the dashboard, it latched the fault while no commands were arriving. Press **Reset faults** to clear it. After a fresh crane boot it clears by itself within a few commands.

| Option | Default | Meaning |
|---|---|---|
| `--port` | (required) | Ground-station port, e.g. COM10 |
| `--rig-port` | none | Crane USB console (COM12): load cross-check, tare / calibrate, power / channel on the crane |
| `--rate` | 20 | Command rate in Hz (ICD: 20–50) |
| `--pa` | LOW | Radio power at start: LOW on the bench (< 2 m), MAX on the rail |
| `--accel` / `--jerk` | 100 / 400 | S-curve limits in %/s and %/s² |
| `--rail-mm` / `--stroke-mm` | 20000 / 1600 | Size of the drawing (rail: MCTR 1 estimate 20–30 m; set to the measured rail and hoist stroke) |
| `--http-port` / `--host` | 5000 / 127.0.0.1 | Web server; `--host 0.0.0.0` lets another PC on the network open it |
| `--open` | off | Open the browser automatically |

**Keep the dashboard tab in the foreground while operating.** A hidden tab currently makes the deadman trip once a minute (B1 will fix this).

---

## Evidence written by every session

`../results/dashboard/<YYYYMMDD_HHMMSS>/`

* **`telemetry.csv`** — one row per command (20 per second). Columns:

  | Group | Columns |
  |---|---|
  | Link | `time, seq, ack, rtt_us, arc` |
  | Every Table 33 field | `msg_type, mode_flags, trolley_cmd, hoist_cmd, magnet_state, segment_idx` |
  | Every decoded Table 34 field | `tlm_seq_ack, trolley_pos_mm, hoist_pos_mm, trolley_vel_mm_s, hoist_vel_mm_s, load_kg, vfd_current_a, magnet_current_a, magnet_status, error_code, fault_flags, system_health` |
  | Both raw 32-byte frames | `downlink_hex, uplink_hex` |

* **`events.log`** — timestamped link, fault, deadman, soft-stop, power / channel and crane events.

These files are the FR12 / FR13 verification evidence for the report. Use them for PER / RTT over a session, load vs reference masses, position vs tape, and the timing of the lost-link stop.

---

## Files

| File | Role |
|---|---|
| `server.py` | Entry point: options, web server, HTTP API |
| `link.py` | Ground-station link: operating sequence, 20 Hz loop, ACK / telemetry handling, deadman, soft stop, power / channel changes, crane console, CSV / log |
| `icd.py` | Table 33 / 34 pack and unpack (`python icd.py` runs its self-test) |
| `ramp.py` | Jerk-limited S-curve ramp (`python ramp.py` prints a 0 → 100 % → 0 profile) |
| `radio.py` | Power levels and the clean-channel list with its survey sources |
| `web/index.html`, `app.js`, `style.css` | The page |

HTTP API (JSON), used by the page:

| Endpoint | Purpose |
|---|---|
| `GET /api/state` | Snapshot |
| `GET /api/icd` | Byte maps |
| `GET /api/history` | Last 30 s |
| `GET /events` | SSE: snapshot every 100 ms + new log events |
| `POST /api/control` | Targets and switches; also the deadman heartbeat |
| `POST /api/action` | `soft_stop`, `release_stop`, `reset`, `zero`, `ping`, `pa`, `channel`, `load_tare`, `load_cal` |

---

## Troubleshooting

| Symptom | Cause / fix |
|---|---|
| `could not open port COM10` | A monitor or the smoke test still holds the port. Close it |
| Link stays **No reply from crane** | Run the smoke test. If G4 fails, the radio link is down; the dashboard can't fix that. Check `cfg_errors=0` and `ce_j3_3=1` on the crane |
| Values move although nothing is connected to the crane | The crane is in `TLM MODEL` (bench model). Only the load cell is measured until the crane interfaces C1–C7 are done |
| Channel selector greyed out | Start with `--rig-port COM12`, and bring trolley and hoist to rest |
| Deadman banner every minute | The tab is in the background. Keep it in front (B1) |
| Load shows **over range** | A J6 signal wire is open or crossed (`saturated=1` on the crane); counts as an overload |
