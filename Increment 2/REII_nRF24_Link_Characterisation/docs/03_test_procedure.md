# 03 — Link characterisation procedure (packet loss vs. distance)

## Fixed parameters (same for every run)

| Parameter | Value | Reason |
|-----------|-------|--------|
| Payload | 32 bytes, static length | Worst case for the future protocol; the SoW plans a 32-byte payload. |
| CRC | 16-bit | Standard; 8-bit lets too many corrupted frames through and inflates "received". |
| Address width | 5 bytes | Default, best false-sync rejection. |
| Pipes | TX→RX on pipe 1 (`LINK_ADDR_DATA`), control on pipe 2 (`LINK_ADDR_CTRL`) | |
| Control configuration | 250 kbps if `isPVariant`, else 1 Mbps; `PA_MAX`; `CTRL_CHANNEL = 80` | Most robust link possible so the test can still be *commanded* when the test config already fails. Channel 80 (2480 MHz) is in-band but above WiFi 1/6/11/13, so it is rarely jammed and stays legal. |
| Burst size `N` | 1000 (`--n 1000`), 200 for smoke tests | 1000 gives 0.1 % PER resolution. |
| Packet spacing | 2 ms in `NOACK` bursts | Prevents the RX FIFO (3 deep) from overflowing at 250 kbps (32 B frame ≈ 1.3 ms on air). |
| ACK burst retry setting | `ARD = 1500 µs`, `ARC = 15` (max) | Upper bound on effective reliability; the recommended (smaller) values are derived from the retry histogram. |

## Variables

| Variable | Levels |
|----------|--------|
| Data rate | 250 kbps (if +), 1 Mbps, 2 Mbps |
| Distance | 1, 2, 5, 10, 15, 20, 30, 50 m line-of-sight; plus ≥ 3 non-LOS points (through a wall / around a corner) that match the crane-lab geometry |
| PA level | `PA_MAX` at every point; `PA_LOW` at 1, 2, 5 m as the saturation control |
| Channel | The channel recommended by the survey (`04_channel_selection.md`). One extra run at a busy WiFi channel (e.g. nRF ch 6 ≈ WiFi ch 1) at 5 m to show the interference effect. |

Full grid: 8 LOS + 3 NLOS = 11 points × 3 rates × (NOACK + ACK) ≈ 66 bursts of 1000 packets. At ~2 ms/packet each NOACK burst is 2 s, each ACK burst < 30 s worst case ⇒ well under an hour of walking.

## Controls / hygiene

* Antennas vertical, 1 m above the floor, same orientation at both ends, person holding the TX stands *behind* it (body away from the RX).
* Measure the distance with a tape or laser once and mark the floor.
* Record: date/time, room, who else is transmitting (ask the other groups), WiFi survey file used.
* Do not run distance tests while the channel survey is running (the survey board is the RX board anyway).
* Randomise nothing — run all rates at one distance before moving; the tool does that for you.

## Running it

RX (NodeMCU-32S) on USB, TX (ESP32-C3) on a power bank, both powered and showing the boot banner.

```bash
cd REII_nRF24_Link_Characterisation
python tools/run_test.py --port /dev/ttyUSB0 --distance 1 --los 1 --pa LOW  --n 200   # smoke
python tools/run_test.py --port /dev/ttyUSB0 --distance 1 --los 1 --pa MAX  --n 1000
python tools/run_test.py --port /dev/ttyUSB0 --distance 2 --los 1 --pa MAX  --n 1000
...
python tools/run_test.py --port /dev/ttyUSB0 --distance 8 --los 0 --pa MAX --note "through lab wall" --n 1000
```

Each invocation, for every rate in `--rates` (default: all supported):

1. RX sends `TEST <mode> <rate> <ch> <pa> <N>` to the TX over the control link (retries for 2 s).
2. Both switch to the test configuration; RX arms a counter; TX sends `N` packets with sequence numbers 0..N-1.
3. RX returns to control config after the last packet or a timeout, reports `unique_rx`, `dup`, `out_of_order`, `rpd_hits`.
4. TX returns to control config and sends `REPORT`: `sent`, `acked`, `failed`, `retries_total`, `max_retries_one_pkt`, `duration_ms`.
5. `run_test.py` appends one CSV row per (mode, rate) to `results/packet_loss_<date>.csv`.

If the control link itself fails (TX out of range at 250 kbps / PA_MAX) the tool logs the row with `status=CTRL_TIMEOUT` — that is the end of the usable range and a valid data point.

## CSV schema (`results/packet_loss_*.csv`)

| column | meaning |
|--------|---------|
| `timestamp` | ISO-8601 |
| `distance_m` | as given on the command line |
| `los` | 1 line-of-sight, 0 obstructed |
| `note` | free text |
| `channel` | nRF `RF_CH` used for the burst |
| `rate_kbps` | 250 / 1000 / 2000 |
| `pa` | MIN / LOW / HIGH / MAX |
| `mode` | NOACK / ACK |
| `n` | packets requested |
| `tx_sent` | packets the TX actually pushed |
| `rx_unique` | unique sequence numbers received (NOACK) |
| `tx_acked` | delivered (ACK mode) |
| `tx_failed` | not delivered after ARC retries (ACK mode) |
| `retries_total` | sum of `ARC_CNT` over the burst |
| `retries_max` | worst single packet |
| `rpd_hits` | RX samples with RPD/CD set during the burst (proxy for "signal > −64 dBm") |
| `duration_ms` | burst wall time measured on the TX |
| `per_pct` | 100 × (1 − rx_unique/n) for NOACK, 100 × tx_failed/n for ACK |
| `status` | OK / CTRL_TIMEOUT / RX_TIMEOUT |

## Plots (deliverable)

```bash
python tools/plot_packet_loss.py results/packet_loss_2026-xx-xx.csv
```

* `results/packet_loss_vs_distance.png` — PER (%) vs distance, one line per data rate, NOACK mode, PA_MAX, LOS solid / NLOS markers.
* `results/ack_reliability_vs_distance.png` — delivered-% and mean retries per packet vs distance per rate.
* Sanity expectation from the datasheet sensitivities (−94 dBm @250k, −85 dBm @1M, −82 dBm @2M on the bare chip; LNA adds ~10 dB): the 250 kbps curve should stay near 0 % noticeably further than 2 Mbps. If it does not, suspect receiver saturation or supply decoupling.
