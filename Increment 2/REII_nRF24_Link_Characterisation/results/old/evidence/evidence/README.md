# Bring-up evidence — `radio.begin() OK` on both nodes (2026-09-05)

Proof that checkpoint goal 1 ("bring up a live nRF24L01 link on an existing dev board") is met.
The `*.log` files in this folder are the **verbatim PlatformIO serial-monitor / PowerShell captures**
from the lab session; nothing was edited apart from cutting them into one file per test. This page
walks through them and points at the lines that constitute the proof.

Setup: NodeMCU-32S (RX, `rx_nodemcu32s`, COM10) and ESP32-C3 (TX, `tx_esp32c3`, COM9), both
USB-powered from the laptop, ~1 m apart, PA+LNA modules with antennas fitted, 10 µF + 100 nF at each
module header, wired as in `../02_wiring.md`.

| # | Test | Log | Proves |
|---|------|-----|--------|
| 1 | RX boot + `INFO` | [`2026-09-05_rx_nodemcu32s_boot_info_ping.log`](2026-09-05_rx_nodemcu32s_boot_info_ping.log) | RX SPI wiring + 3V3 OK, module is nRF24L01+ |
| 2 | TX `INFO` | [`2026-09-05_tx_esp32c3_info.log`](2026-09-05_tx_esp32c3_info.log) | TX SPI wiring + 3V3 OK, module is nRF24L01+ |
| 3 | `PING` → `PONG` | same file as 1 | RF round trip RX → TX → RX on the control config |
| 4 | `BEACON` / `LISTEN` | [`2026-09-05_tx_esp32c3_beacon.log`](2026-09-05_tx_esp32c3_beacon.log), [`2026-09-05_rx_nodemcu32s_listen.log`](2026-09-05_rx_nodemcu32s_listen.log) | Sustained one-way packet stream on the test config, no loss |
| 5 | `run_test.py` at 1 m | [`2026-09-05_run_test_1m_pa_low.log`](2026-09-05_run_test_1m_pa_low.log) | Full automated pipeline PC → RX → TX → RX → CSV, 3 rates × 2 modes |

---

## 1. RX: `radio.begin() OK` and register read-back

From `2026-09-05_rx_nodemcu32s_boot_info_ping.log` (the ESP32 ROM boot lines precede it; the first
boot's ROM text is mangled because the ROM prints at 74880 baud — normal, not a fault):

```
# REII nRF24 linktest v0.1.0 role=RX board=NodeMCU-32S ctrl_ch=80
radio.begin() OK, isPVariant=1
SPI Frequency           = 4 Mhz
Channel                 = 80 (~ 2480 MHz)
Model                   = nRF24L01+
RF Data Rate            = 250 KBPS
RF Power Amplifier      = PA_MAX
RF Low Noise Amplifier  = Enabled
CRC Length              = 16 bits
Address Length          = 5 bytes
Static Payload Length   = 32 bytes
Auto Retry Delay        = 1500 microseconds
Auto Retry Attempts     = 15 maximum
Auto Acknowledgment     = Enabled
Primary Mode            = RX
TX address              = 0x3249494552
pipe 0 (closed) bound   = 0x3249494552
pipe 1 ( open ) bound   = 0x3149494552
READY
```

Why this is proof and not just a print-out:

* `radio.begin()` in the RF24 library configures the chip over SPI and **reads `RF_SETUP` back**; it
  returns `false` (firmware prints `radio.begin() FAILED - check 3V3, CE, CSN, SCK, MOSI, MISO`)
  if the read-back is `0x00` or `0xFF`, i.e. no chip answering. `OK` means MOSI, MISO, SCK, CSN
  and the 3V3 supply all work.
* `isPVariant=1` — the library toggled the 250 kbps bit in `RF_SETUP` and read it back set, so the
  module is an nRF24L01**+** (250 kbps and RPD available), as assumed in the README.
* Every value in the `INFO` dump is read from the chip, not from firmware variables. They match what
  `radio_cfg.cpp` writes: 5-byte addresses, CRC-16, 32-byte static payload, ARD 1500 µs / ARC 15,
  control channel 80, 250 kbps.
* `TX address = 0x3249494552` is the bytes `52 45 49 49 32` = ASCII **`REII2`** (the peer); pipe 1
  `0x3149494552` = **`REII1`** (own address). A floating MISO would read `0x00`/`0xFF` here.

## 2. TX: same read-back on the ESP32-C3

From `2026-09-05_tx_esp32c3_info.log`:

```
INFO
SPI Frequency           = 4 Mhz
Channel                 = 80 (~ 2480 MHz)
Model                   = nRF24L01+
RF Data Rate            = 250 KBPS
...
TX address              = 0x3149494552
pipe 0 (closed) bound   = 0x3149494552
pipe 1 ( open ) bound   = 0x3249494552
```

Identical register set to the RX, with the addresses mirrored (TX writes to `REII1`, listens on
`REII2`) — exactly the pairing the RX expects. The `Disconnected … Reconnecting to COM9` lines and
the repeated `# REII … READY` banners in that file are the Windows USB-CDC port re-enumerating and the
firmware answering bare Enter presses (see PR #5); they are not resets of the radio.

The TX prints the same `radio.begin() OK, isPVariant=1` line at boot, but the C3's native USB-CDC
discards output before the monitor attaches, so it is not in this capture. The proof on the TX side
is the line `READY (waiting for commands from RX)`: in `role_tx.cpp` it is only reached after
`radioBegin()` returns true — on failure the firmware loops forever printing `radio.begin() FAILED`
and never gets to `READY`. The successful `INFO` read-back above is the second, independent proof.

## 3. Over-the-air round trip — `PONG,1,3`

Still in `2026-09-05_rx_nodemcu32s_boot_info_ping.log`, typed on the RX monitor:

```
PING
PONG,1,3
```

`PONG,<success>,<elapsed_ms>`: the RX sent a `CMD_PING` frame to `REII1` on the control config
(ch 80, 250 kbps, auto-ack on) and got the TX's hardware acknowledgement back **3 ms** later; `1` =
`radio.write()` returned true, i.e. the TX received the frame CRC-clean *and* the RX received the
ACK the TX's chip sent in reply. Both directions of the air link work. (The TX monitor prints a
`PING` line for each one it receives.)

## 4. Sustained packet stream — 233 / 233, no gaps

TX monitor (`2026-09-05_tx_esp32c3_beacon.log`): `BEACON 1000 76 LOW` → 1 Mbps, RF channel 76,
PA_LOW, 10 packets/s, NOACK.

```
BEACON,sent=16,ok=16
BEACON,sent=32,ok=32
…
BEACON,sent=656,ok=656
STOPPED
```

RX monitor (`2026-09-05_rx_nodemcu32s_listen.log`): `LISTEN 1000 76 LOW`, format
`PKT,<rx_ms>,<magic>,<seq>,<tx_ms>,<rpd>`:

```
PKT,75,5A,424,183317,1
PKT,175,5A,425,183417,1
…
PKT,23175,5A,655,206417,1
PKT,23275,5A,656,206517,1
STOPPED, packets=233
```

* Sequence numbers run **424 → 656 with no missing value** = 233 packets, and `packets=233` agrees.
  (The RX started listening after the TX had already sent 423 packets; that is why it does not start
  at 1.)
* `5A` is the frame magic — every payload was decoded correctly (CRC-16 passed, right length).
* The RX and TX timestamps both advance in exact 100 ms steps, matching the 10 pkt/s beacon rate.
* Last field `1` on every line = RPD flag set = received power above −64 dBm at 1 m with PA_LOW.

## 5. Automated pipeline — `run_test.py` at 1 m

`2026-09-05_run_test_1m_pa_low.log`:

```
python tools/run_test.py --port COM10 --distance 1 --pa LOW --n 200
  NOACK  250 kbps ch76 PA_LOW: PER=0.50%  rx_unique=199/200  acked=0   failed=0 retries=0 (max 0)  rpd=601/662  OK
  ACK    250 kbps ch76 PA_LOW: PER=0.00%  rx_unique=200/200  acked=200 failed=0 retries=1 (max 1)  rpd=259/694  OK
  NOACK 1000 kbps ch76 PA_LOW: PER=0.00%  rx_unique=200/200  acked=0   failed=0 retries=0 (max 0)  rpd=601/660  OK
  ACK   1000 kbps ch76 PA_LOW: PER=0.00%  rx_unique=200/200  acked=200 failed=0 retries=0 (max 0)  rpd=131/441  OK
  NOACK 2000 kbps ch76 PA_LOW: PER=0.50%  rx_unique=199/200  acked=0   failed=0 retries=0 (max 0)  rpd=599/659  OK
  ACK   2000 kbps ch76 PA_LOW: PER=0.00%  rx_unique=200/200  acked=200 failed=0 retries=1 (max 1)  rpd=86/402   OK
6 rows appended to ...\results\packet_loss_2026-09-05.csv
```

Each row is one complete cycle of the real measurement protocol: PC → RX (`TEST …`), RX → TX
(`TEST` frame on the control config), both switch to ch 76 at the given rate, TX sends 200 numbered
frames, both return to the control config, TX → RX (`REPORT` frame with acked/failed/retry counts),
RX → PC (`RESULT,…` line), PC appends CSV. `OK` in the status column means the TX report arrived —
i.e. the control link survived every rate change. Six for six.

## What this does **not** yet prove

Range, behaviour in interference, and the best channel — those are README §3–4 (channel survey and
distance sweep). See `../06_progress_summary.md` §4.
