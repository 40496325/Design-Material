# 04 — Channel selection with interference analysis

## What shares the band

| Source | Where it sits | How we see it |
|--------|---------------|---------------|
| Lab / campus WiFi (802.11b/g/n, 20 MHz) | WiFi ch *n* centre = 2407 + 5·n MHz, occupies ±11 MHz (22 MHz for b, 20 MHz for g/n). Typical APs on 1 / 6 / 11 (and 13 in ZA). | ESP32 `WiFi.scanNetworks()` in the `scanner` role → SSID, channel, RSSI. |
| Other groups' nRF24 links (up to 4 other REII-type links running at once) | 1 MHz (1 Mbps) or 2 MHz (2 Mbps) wide, on whatever `RF_CH` they picked | nRF24 carrier-detect (`RPD`/`CD`) sweep — only visible while they transmit, so survey **while the others are running**. |
| Bluetooth / BLE (phones, laptops) | 2402–2480 MHz frequency hopping, 1–2 MHz | Shows up as a low, flat CD floor across the band — cannot be avoided, only tolerated with retries. |
| Microwave ovens, USB-3 noise | 2450–2470 MHz-ish / broadband | Occasional spikes in the CD sweep. |

WiFi ↔ nRF channel mapping (`RF_CH = F − 2400`):

| WiFi ch | centre MHz | nRF channels covered (±11 MHz) |
|---------|-----------|--------------------------------|
| 1  | 2412 | 1 – 23 |
| 6  | 2437 | 26 – 48 |
| 11 | 2462 | 51 – 73 |
| 13 | 2472 | 61 – 83 |

Consequences:

* If the lab uses WiFi 1/6/11 only, the natural **quiet gaps** are nRF **24–25**, **49–50** and **74–81**.
* The 2.4 GHz ISM band is 2400.0–2483.5 MHz (ICASA / ETSI / FCC). nRF channels **≥ 84** are outside it, and 82–83 (and 0–1) put a 2 MHz-wide 2 Mbps signal on the band edge. The tool therefore restricts the recommendation to **2–81** unless `--allow-above-ism` is passed, and lists the out-of-band channels separately for information only. The firmware **control channel is 80** (in-band, above WiFi 11) — the recommendation excludes it so the two links never share a frequency.
* At 2 Mbps the nRF occupies 2 MHz, so two groups need `|RF_CH_a − RF_CH_b| ≥ 2` (`NRF24L01.PDF` §6.3); at 1 Mbps / 250 kbps ≥ 1. We propose ≥ 4 to keep some margin against the +'s spectral skirts (1st adjacent-channel C/I ≈ +8 dB at 1 Mbps, Table 11).

## Survey method (`scanner` role on the NodeMCU-32S)

1. **RPD sweep.** For each channel 0..125: `setChannel(ch)`, `startListening()`, wait 200 µs (> 128 µs required by §6.1.3), read `testRPD()` / `testCarrier()`, `stopListening()`. Repeat the whole sweep `S` times (default 200 → 25 s). Output per channel: `hits/S`. Datasheet: RPD sets when the received power is > −64 dBm (+ variant), so a hit means *strong* interference at that instant, not "any energy".
2. **WiFi scan.** `WiFi.mode(WIFI_STA); scanNetworks(async=false, show_hidden=true)`. Output per AP: `ssid, bssid, wifi_ch, rssi_dbm`. Then `WiFi.mode(WIFI_OFF)`.
3. Both are streamed over serial as CSV blocks (`RPD,ch,hits,sweeps` and `WIFI,ssid,bssid,ch,rssi`) and captured by `tools/channel_analysis.py`.
4. Repeat the survey (a) with the lab quiet, (b) with all groups' links running, (c) at the position where the ground-station dongle will sit.

## Scoring (implemented in `tools/channel_analysis.py`)

For every nRF channel `c` in 2..81 (excluding the control channel 80):

```
wifi_penalty(c) = Σ_APs  w(rssi) · overlap(c, ap_ch)      overlap = 1 inside ±11 MHz (raised-cosine tapered at the edges)
                                                               w(rssi) = 10^((rssi + 100)/20), clamped   (−40 dBm AP weighs ~1000× a −100 dBm one)
rpd_penalty(c)  = hits(c)/sweeps                              (0..1)
score(c)        = 0.5 · norm(wifi_penalty) + 0.5 · rpd_penalty   (lower is better)
```

Then the tool

* prints the 10 best channels, and the best channel **with a 4-channel guard band** free on both sides (for coexistence with 4 other groups),
* proposes a **group channel plan**: five channels ≥ 4 apart drawn from the quietest region (so all REII-type links can run simultaneously),
* writes `results/channel_survey_<date>.png` (RPD occupancy bars + WiFi AP overlay + chosen channel).

## Verification of the chosen channel

The chosen channel is then used for the distance sweep (`03`). One additional 5 m run on a deliberately bad channel (centre of the strongest WiFi AP) is done to quantify the benefit — that comparison is the "interference analysis" figure in the report.
