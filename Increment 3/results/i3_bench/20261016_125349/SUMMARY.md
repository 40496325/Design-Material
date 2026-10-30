# REII I3 Bench Evidence — 20261016_125349

## Overall result: FAIL

### Ground station (NodeMCU-32S, COM10)
- USB-CDC echo: OK
- radio.begin() OK: FAIL
- isPVariant=1: OK
- Static 32-byte payload: OK
- CRC-16: OK
- 5-byte address: OK
- Auto-ACK enabled: OK
- PING->PONG: OK
  - PONG ack=1: FAIL
  - RTT: 48 ms
- APP config (ch77/2M/PA_MAX/ARC5/ARD500): OK

### Rig (ESP32-S3, COM12)
- nRF24 link up (listening on REII2): FAIL
- PRX on 'REII2': FAIL
- ch77 / 2 Mbps: FAIL
- PINGs received by rig: 200

### Smoke burst (N=200 PING/PONG at app config)
- Successes: 192
- Failures: 8
- PER: 4.0%
- RTT min/max/avg: 0 / 2 / 0.89 ms

### Artefacts
- `groundstation.log` — full ground-station serial capture (timestamped)
- `rig.log` — full rig serial capture (timestamped)
- `smoke.csv` — per-round PING/PONG results
- `index.json` — machine-readable evidence index
