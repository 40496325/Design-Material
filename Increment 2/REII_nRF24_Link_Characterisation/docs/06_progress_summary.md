# Progress summary — nRF24L01 link bring-up (checkpoint status, 2026-09-05)

Status report against the REII *Remote Control Dashboard* checkpoint slide, written after the first
lab session in which both radios were brought up and the first logged measurement was taken.
Everything in section 3 is measured on real hardware; everything in section 4 is still to do.

## 1. Checkpoint goals vs. status

| # | Checkpoint task (slide) | Status | Evidence |
|---|-------------------------|--------|----------|
| 1 | Bring up a live nRF24L01 link on an existing dev board / breadboard — not the from-scratch STM32F411 dongle yet | **Done** | §3.1–3.4: `radio.begin() OK` on both boards, `PONG,1,3`, 233/233 beacon packets, smoke-test CSV |
| 2 | Characterise the link systematically: packet loss vs. distance, ≥ 2 data rates | **Tooling done, first point measured** | §3.4: PER at 1 m for 250 kbps / 1 Mbps / 2 Mbps, NOACK and ACK. Distance sweep is the next lab session (§4) |
| 3 | Identify the best channel for the lab (WiFi + other groups' links) | **Tooling done, survey not yet run** | Scanner firmware + `tools/channel_analysis.py` (dry-run on synthetic data only) |
| 4 | Prove the radio is reliable before the dongle build / protocol design are committed to | **Bring-up proven; range/robustness pending** | §3.4 |

"Bring this" deliverables:

| Deliverable | Status |
|-------------|--------|
| Packet-loss vs. distance curves, ≥ 2 data rates | Pipeline verified end-to-end at 1 m (3 rates × 2 modes → CSV). Curves need the distance sweep. |
| Channel selection with interference analysis | Method + scoring implemented (`docs/04_channel_selection.md`); lab survey outstanding. |
| Recommended nRF24 configuration (channel, data rate, power, retries) | Template ready (`docs/05_recommended_config.md`); to be filled from measured data. Address width 5, CRC-16, 32-byte payload, ARD = 1500 µs / ARC = 15 already validated in the bring-up. |

## 2. What was built (software + documentation)

Everything lives in `REII_nRF24_Link_Characterisation/` and was merged via PRs #2 – #5.

### 2.1 Hardware plan and wiring
* `docs/01_hardware_selection.md` — why NodeMCU-32S = RX / ground-station stand-in (also scanner),
  ESP32-C3 = mobile TX on a power bank, Arduino Nano = fallback TX (needs an external 3.3 V LDO for
  the PA+LNA module; its on-board 3V3 pin cannot supply the TX burst).
* `docs/02_wiring.md` — pin maps for all three boards; kept in sync with
  `firmware/lib/linktest/board_pins.h`.
* `docs/wiring/*.drawio` + PNG — one draw.io diagram per node (RX NodeMCU-32S; TX ESP32-C3 with the
  Nano fallback on page 2), generated from the pin tables by `tools/gen_wiring_drawio.py`. Each shows
  the 10 µF + 100 nF decoupling at the module header and the "antenna on before power" warning.

Pins actually used in the lab (verified by the register read-back in §3.2):

| nRF24 | RX: NodeMCU-32S | TX: ESP32-C3 |
|-------|-----------------|--------------|
| VCC / GND | 3V3 / GND | 3V3 / GND |
| CE | GPIO4 | GPIO10 |
| CSN | GPIO5 | GPIO7 |
| SCK | GPIO18 | GPIO4 |
| MOSI | GPIO23 | GPIO6 |
| MISO | GPIO19 | GPIO5 |

### 2.2 Firmware (`firmware/`, PlatformIO, Arduino framework, RF24 library)
One code base, three roles chosen at build time (`-DROLE_RX / _TX / _SCANNER`), five build environments
(`rx_nodemcu32s`, `scanner_nodemcu32s`, `tx_esp32c3`, `tx_esp32c3_uart`, `tx_nano`).

* **RX (master)** — the only board the PC talks to (stand-in for the future STM32 dongle). Serial
  commands `INFO`, `PING`, `TEST <NOACK|ACK> <rate> <ch> <pa> <n> [spacing]`, `LISTEN …`.
  Commands the TX over a fixed *control configuration* (ch 80, 250 kbps, PA_MAX, auto-ack), both
  sides switch to the *test configuration*, RX counts unique/duplicate/out-of-order sequence numbers
  and samples the RPD (received-power) flag at ~1 kHz, then prints one `RESULT,…` CSV line.
* **TX (mobile)** — waits for `TEST`, blasts `n` numbered 32-byte frames, reports its own view
  (acked / failed / retransmit count from `OBSERVE_TX`) back over the control link.
  Manual `BEACON <rate> <ch> <pa>` for bring-up.
* **Scanner** — sweeps all 126 nRF channels reading RPD (nRF24L01+) and lists WiFi APs with the
  ESP32's own radio; output feeds `tools/channel_analysis.py`.
* Common: 5-byte addresses (`REII1` / `REII2`), CRC-16, static 32-byte payload, ARD 1500 µs, ARC 15,
  SPI at 4 MHz (breadboard-safe), ESP32 WiFi/BT switched off so they cannot interfere with the
  measurement.

### 2.3 PC tooling (`tools/`, Python)
* `run_test.py` — drives the RX over serial, runs NOACK + ACK bursts at 250k / 1M / 2M for a given
  distance / channel / PA, appends to `results/packet_loss_<date>.csv`.
* `plot_packet_loss.py` — PER vs. distance per data rate, plus ACK-retry plot.
* `channel_analysis.py` — scores channels from scanner output (RPD occupancy + WiFi overlap),
  restricted to ch 2–81 so a 2 Mbps signal stays inside 2400–2483.5 MHz; recommends a channel.
* `make_synthetic_data.py` — dry-run data for the two analysis scripts (never committed).

### 2.4 Lab-session fixes (PRs #4, #5)
* PlatformIO monitor set to echo + send-on-Enter so firmware commands can be typed in VS Code.
* ESP32-C3 native USB-CDC discards output printed before the host attaches: firmware now waits up to
  3 s for the USB host and re-prints its banner on a bare Enter. Root cause of the "silent C3" during
  bring-up was this plus monitoring with the `_uart` build on a native-USB board (`VID:PID 303A:1001`).

## 3. What was measured (lab session, 2026-09-05)

Setup: NodeMCU-32S (RX, COM10) and ESP32-C3 (TX, COM9) both USB-powered from the laptop, ~1 m apart,
both PA+LNA modules with antennas fitted and 10 µF + 100 nF at the module header.
The verbatim serial-monitor captures behind every quote below are in `evidence/` (see
`evidence/README.md` for the line-by-line proof).

### 3.1 SPI / power check — both boards
Boot output on the RX (identical on the TX apart from the role and swapped addresses):

```
# REII nRF24 linktest v0.1.0 role=RX board=NodeMCU-32S ctrl_ch=80
radio.begin() OK, isPVariant=1
Model                   = nRF24L01+
Channel                 = 80 (~ 2480 MHz)
RF Data Rate            = 250 KBPS
RF Power Amplifier      = PA_MAX
CRC Length              = 16 bits
Address Length          = 5 bytes
Static Payload Length   = 32 bytes
Auto Retry Delay        = 1500 microseconds
Auto Retry Attempts     = 15 maximum
TX address              = 0x3249494552      ("REII2")
pipe 1 ( open ) bound   = 0x3149494552      ("REII1")
```

Every register reads back exactly what the firmware wrote → MOSI, MISO, SCK, CSN and 3V3 are
correct on both boards. `isPVariant=1` confirms the modules are nRF24L01+ (250 kbps and RPD available),
as assumed in the README.

### 3.2 Control link — RX → TX → RX round trip
```
PING
PONG,1,3
```
TX answered on the control configuration; 3 ms round trip.

### 3.3 Bring-up streaming test — 1 Mbps, ch 76, PA_LOW, 10 pkt/s
TX: `BEACON 1000 76 LOW` → `BEACON,sent=656,ok=656`.
RX: `LISTEN 1000 76 LOW` → sequence numbers 424 … 656 received consecutively,
`STOPPED, packets=233` — **233 of 233 packets in the listening window, no gaps**, RPD flag = 1
(> −64 dBm) on every packet.

### 3.4 First logged measurement — `run_test.py --distance 1 --pa LOW --n 200`
`results/packet_loss_2026-09-05.csv`:

| Mode | Rate | Ch | PA | PER | rx_unique | acked / failed | retries (max) |
|------|------|----|----|-----|-----------|----------------|---------------|
| NOACK | 250 kbps | 76 | LOW | 0.50 % | 199 / 200 | – | – |
| ACK | 250 kbps | 76 | LOW | 0.00 % | 200 / 200 | 200 / 0 | 1 (1) |
| NOACK | 1 Mbps | 76 | LOW | 0.00 % | 200 / 200 | – | – |
| ACK | 1 Mbps | 76 | LOW | 0.00 % | 200 / 200 | 200 / 0 | 0 (0) |
| NOACK | 2 Mbps | 76 | LOW | 0.50 % | 199 / 200 | – | – |
| ACK | 2 Mbps | 76 | LOW | 0.00 % | 200 / 200 | 200 / 0 | 1 (1) |

Reading: at 1 m, PA_LOW, raw PER is 0–0.5 % (one lost packet in 200) at all three rates, and with
auto-ack every packet was delivered with at most one retransmission. The RX-commands-TX control link
held throughout. This is the reference point for the distance sweep; nothing can be said about range
or channel yet.

## 4. Still to do (README §3–4)

1. **Channel survey** — flash `scanner_nodemcu32s`, TX powered off,
   `python tools/channel_analysis.py --port COM10 --out results` → occupancy plot + WiFi list +
   recommended channel. Repeat once while other groups' links are active.
2. **Distance sweep** — TX on a power bank, `run_test.py --pa MAX --n 1000 --channel <recommended>`
   at 1, 2, 5, 10, 15, 20, 30, 50 m LOS and 2–3 NLOS points; `plot_packet_loss.py` → PER vs.
   distance curves for 250 kbps / 1 Mbps / 2 Mbps.
3. **Fill in `docs/05_recommended_config.md`** (channel, data rate, PA level, ARD/ARC) from 1 + 2, and
   commit `results/`.
