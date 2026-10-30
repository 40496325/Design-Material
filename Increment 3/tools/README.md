# REII I3 Bench Tools

Evidence-capture tooling for the Increment 3 bench-exit gate
(see `docs/27_reii_nrf_wiring_and_config.md` §5).

## Files

| File | Purpose |
|------|---------|
| `capture_evidence.py` | Dual-port serial capture: runs ECHO→INFO→PING→APP on the ground station, captures the rig's nRF24 link-up banner, then runs an N=200 PING/PONG smoke burst at the app config. Writes timestamped logs, `smoke.csv`, `index.json`, and `SUMMARY.md` to `results/i3_bench/<run_id>/`. |
| `capture_evidence.ps1` | PowerShell wrapper with sensible defaults. Edit `-gs-port`/`-rig-port` to match your COM ports. |

## Prerequisites

```
pip install pyserial
```

## Usage (PowerShell)

```powershell
# 1. List available serial ports to find your COM numbers
.\capture_evidence.ps1 -ListPorts

# 2. Run the full evidence capture (edit ports as needed)
.\capture_evidence.ps1 -gs-port COM10 -rig-port COM12 -smoke-n 200
```

## Usage (Python directly)

```bash
python capture_evidence.py --gs-port COM10 --rig-port COM12 --smoke-n 200
python capture_evidence.py --list-ports
```

## What gets captured

**Ground station (NodeMCU-32S):**
1. `ECHO` — proves USB-CDC before touching RF
2. `INFO` — `radio.begin() OK`, `isPVariant=1`, `printPrettyDetails` (RF_CH/RF_SETUP/SETUP_RETR match Table 2)
3. `PING` — `PONG,1,<ms>` round-trip vs the rig
4. `APP` — switch to ch77/2M/PA_MAX/ARC5/ARD500

**Rig (ESP32-S3):**
- Boot banner (ESP-IDF v6.1, 16 MB flash, peripherals OK)
- nRF24 link up: listening on `REII2` ch77/2Mbps
- `nrf: PING #N from groundstation` — rig received the PING
- `nrf: listening - frames=N` — periodic heartbeat

**Smoke burst:** N PING/PONG rounds at the app config, per-round RTT logged to `smoke.csv`.

## Evidence output

```
results/i3_bench/20261016_143000/
├── groundstation.log   # timestamped ground-station serial capture
├── rig.log             # timestamped rig serial capture
├── smoke.csv           # per-round PING/PONG results (round, ack, rtt_ms, timestamp)
├── index.json          # machine-readable evidence index
└── SUMMARY.md          # human-readable PASS/FAIL summary
```

## Notes

- **Both ports are needed** for evidence: the ground station proves it sent PING and got PONG (round-trip), the rig proves it received PING and responded (link is up on both sides).
- The rig must already be flashed and running (its `nrf_link_task` polls for PING frames and auto-ACKs them back).
- Use `PA_LOW` for bench tests < 1 m to avoid LNA saturation (see `docs/04_rf_baseline.md`).
- Press Ctrl+C to abort; both ports are closed cleanly.