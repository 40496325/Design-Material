# groundstation_C3: ESP32-C3 ground station (spare for the NodeMCU-32S)

This is a second ground station built on the ESP32-C3 that was the mobile TX in the Increment 2 characterisation. It runs the **same firmware, serial protocol and radio settings** as `..\groundstation_PIO` v4.1.0, so the dashboard and `smoke_test.py` work unchanged. Only the board pins are different.

`..\groundstation_PIO` and the NodeMCU are **not touched**. To go back, plug the NodeMCU in and carry on as before.

| | NodeMCU-32S (`groundstation_PIO`) | ESP32-C3 (`groundstation_C3`) |
|---|---|---|
| Firmware | v4.1.0 | v4.1.0-c3 (same code, C3 pin block) |
| USB | CP2102 USB-UART, COM10 | Native USB-CDC (303A:1001), COM9 in Increment 2 |
| nRF pins CE / CSN / SCK / MISO / MOSI | 4 / 5 / 18 / 19 / 23 | **10 / 7 / 4 / 5 / 6** |
| Radio config | ch 77, 2 Mbps, ARC 5, ARD 500 µs, PA LOW, ICD profile | identical |
| Wi-Fi / BT | off | off (same `WiFi.mode(WIFI_OFF); btStop();`) |

---

## 1. Hardware: rewiring

### 1.1 Which radio module?

* **A spare PA+LNA module is available (preferred):** build the C3 as its own unit. The NodeMCU stays wired and you can switch between them in 10 s.
* **Only the NodeMCU's module is available:** move it to the C3. The NodeMCU is then out of action until it is rewired back to the Increment 2 RX table (`docs/02_wiring.md`).

### 1.2 Parts

* ESP32-C3 dev board (the Increment 2 TX board, native USB)
* nRF24L01+ PA+LNA module with its SMA antenna
* 10 µF electrolytic/tantalum and 100 nF ceramic capacitors, soldered across VCC/GND **at the module header**
* 7 short female-female leads (≤ 10 cm), or solder the module onto stripboard
* A USB-C **data** cable ≤ 2 m (G1). Charge-only cables are the most common cause of "no COM port".

### 1.3 Pin table

nRF24 header (component side, square pad = GND):

```
        ┌───────┐
  GND ──┤ 1   2 ├── VCC (3.3 V only)
   CE ──┤ 3   4 ├── CSN
  SCK ──┤ 5   6 ├── MOSI
 MISO ──┤ 7   8 ├── IRQ (not connected)
        └───────┘
```

| nRF pin | Signal | ESP32-C3 GPIO | Board label (SuperMini / DevKitM-1) | Note |
|---|---|---|---|---|
| 1 | GND | GND | `G` / `GND` | |
| 2 | VCC | 3V3 | `3.3` / `3V3` | **Never the 5V pin.** 10 µF + 100 nF at the module |
| 3 | CE | **10** | `10` / `IO10` | |
| 4 | CSN | **7** | `7` / `IO7` | FSPICS0 |
| 5 | SCK | **4** | `4` / `IO4` | FSPICLK |
| 6 | MOSI | **6** | `6` / `IO6` | FSPID |
| 7 | MISO | **5** | `5` / `IO5` | FSPIQ |
| 8 | IRQ | — | — | not used by the firmware |

These are the Increment 2 TX pins (`board_pins.h`, `docs/02_wiring.md`), already proven with this module.

**Do not use** GPIO2/8/9 (strapping pins; GPIO8 is the LED and GPIO9 the BOOT button on most C3 boards), GPIO18/19 (USB D−/D+), GPIO12–17 (module flash) or GPIO20/21 (UART0).

### 1.4 Rewiring steps

1. Unplug USB. **Screw the antenna on first**; transmitting with no antenna can damage the PA.
2. If moving the module: remove the 7 leads from the NodeMCU and label them by signal, not by colour.
3. Solder or fit 10 µF + 100 nF across module pins 1–2, as close to the header as possible.
4. Wire per the table. Keep the SPI leads short and together, away from the antenna.
5. **Continuity check** (meter, USB unplugged): each nRF pin to its GPIO, and **no short between 3V3 and GND**.
6. Plug in USB. Measure **3.3 V ± 0.1 V at module pins 1–2**, not at the board.
7. Mount it: antenna vertical, module and capacitors strain-relieved (G1), C3 at least 10 cm from the antenna.

### 1.5 Power check (why the C3 is fine)

| Load | Current |
|---|---|
| ESP32-C3, Wi-Fi/BT off, 160 MHz | ≈ 25 mA |
| nRF24L01+ PA+LNA, TX burst (design figure) | ≤ 150 mA |
| **Total peak** | **≈ 175 mA** |

That is well inside the board's 3.3 V regulator (≈ 500 mA class) and the 500 mA a USB port supplies. The 10 µF covers the fast PA current edges that the regulator cannot follow.

---

## 2. Firmware changes (already made in this folder)

```
groundstation_C3/
├── platformio.ini        envs: groundstation_esp32c3 (default, native USB), groundstation_esp32c3_uart
├── include/gs_config.h   + BOARD_ESP32C3 pin block, GS_PIN_HINT, GS_USB_HOST_WAIT_MS
├── include/icd_frames.h  unchanged copy
└── src/main.cpp          3 small changes, listed below
```

| File | Change | Why |
|---|---|---|
| `platformio.ini` | New env `groundstation_esp32c3`: `board = esp32-c3-devkitm-1`, `-DBOARD_ESP32C3 -DARDUINO_USB_MODE=1 -DARDUINO_USB_CDC_ON_BOOT=1`, `monitor_dtr/rts = 0`, COM9. A `_uart` env is included for C3 boards with a CH340/CP2102 chip | Same flags as `tx_esp32c3` in Increment 2 |
| `gs_config.h` | `BOARD_ESP32C3` block: CE 10, CSN 7, SCK 4, MISO 5, MOSI 6. `GS_BOARD_NAME "ESP32-C3"`, version `4.1.0-c3` | Pins; the banner shows which board is plugged in |
| `gs_config.h` / `main.cpp` | Pin text in the `radio.begin() FAILED` / `ERR,radio not responding` messages comes from `GS_PIN_HINT` | The old text named the NodeMCU pins |
| `main.cpp` `setup()` | Waits up to 3 s for the PC to open the USB-CDC port before printing the banner (`while (!Serial && …)`), only on the C3 build | USB-CDC drops anything printed before the host connects. This was the "silent C3" in Increment 2 |
| Unchanged | Addresses, channels, ARC/ARD, PA, ICD/STATIC profiles, every serial command and reply format (`ECHO`, `APP`, `PROFILE`, `PA`, `CH`, `PING`, `CMD`, `STATS`, `SCAN`, `RPD`, `CARRIER`) | The dashboard and smoke test need no changes |

`SPI.begin(SCK, MISO, MOSI, CSN)` uses the C3's GPIO matrix, so the same call works with the new pins. SPI stays at 4 MHz.

**Not compile-checked yet.** The cloud workspace could not download the espressif32 platform. The new lines use the same calls and build flags as the Increment 2 `tx_esp32c3` code, which built and ran on this board. The first build on your PC is the check.

---

## 3. Build and flash (PlatformIO, same as before)

1. Close every serial monitor, the dashboard and `smoke_test.py`. Only one program can hold a COM port.
2. Plug in the C3 only, so the port is unambiguous. In **Device Manager → Ports** note its COM number. Native USB shows as *USB Serial Device* / *USB JTAG/serial debug unit*. If it isn't COM9, change `upload_port` and `monitor_port` in `platformio.ini`.
3. VS Code → **File → Open Folder** → `Increment 4\groundstation_C3`, opened on its own like `groundstation_PIO`. Wait for "PlatformIO: Project initialised".
4. In the blue status bar choose the env **`env:groundstation_esp32c3`**.
5. **Build** (✓) → **Upload** (→) → **Monitor** (🔌). Or from a terminal in the folder:
   ```powershell
   pio run -e groundstation_esp32c3 -t upload
   pio device monitor -e groundstation_esp32c3
   ```
6. **If the upload fails** with *Failed to connect* or no port: hold **BOOT**, tap **RESET**, release **BOOT**, then upload again. Press **RESET** afterwards to run the new firmware. Unplug and replug USB if the port doesn't come back.

---

## 4. Testing

**Recommended order:** monitor check → smoke test → dashboard. The dashboard will very likely work straight away, because it uses only `ECHO`, `APP`, `PROFILE`, `PA`, `CH`, `INFO` and `CMD`, all unchanged, and it opens the port with DTR/RTS low. But when the dashboard fails it only says `check_failed`. The smoke test says *which* end is at fault, and its run folder is your evidence that the spare meets the ICD. It takes about 2 minutes.

### Step 1: Monitor check (no crane needed, ~2 min)

In the PlatformIO monitor (press **Enter** once if the screen stays blank):

```
# REII groundstation v4.1.0-c3 board=ESP32-C3 app_ch=77 ctrl_ch=80 (Increment 4)
radio.begin() OK, isPVariant=1
... printPrettyDetails register dump ...
GS,LINK,mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=ICD,payload=dyn32+ackpl32,tx_to=REII2,radio=OK
READY
```

| Check | Pass |
|---|---|
| Banner | `board=ESP32-C3`, `v4.1.0-c3` |
| SPI / wiring | `radio.begin() OK`, `isPVariant=1`, `radio=OK` |
| Register read-back | Channel 77, 2 MBPS, CRC 16 bits, 5-byte addresses, TX address `0x3249494552`. Values of all `0x00` or all `0xFF` mean a MISO/SCK fault |
| `ECHO hello` | `ECHO,hello` |
| `SCAN 20` | 126 `SCAN,` lines + `SCAN_END`. Wi-Fi shows as hits around ch 12–62; ch 77 should be near 0 |

**Close the monitor before step 2.**

### Step 2: Smoke test with the crane (~2 min)

Crane powered and booted (`cfg_errors=0`, `ce_j3_3=1`), antennas ≥ 0.5 m apart, PA LOW on the bench.

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\tools"
python smoke_test.py --gs-port COM9 --rig-port COM12 --no-reset
```

Use **`--no-reset`** with the C3. The script's reset is an RTS pulse into the NodeMCU's CP2102 auto-reset circuit, and the C3's native USB handles DTR/RTS differently. It isn't needed: the firmware boots straight into the ICD mode and G2 checks the radio with `INFO`. G2 may then not print the `isPVariant=1` line (the banner happened before the port opened). That is only missing, not a fail; step 1 already confirmed it. The gate title still says "NodeMCU-32S"; that is cosmetic.

**Pass (ICD Table 32, same gates as the NodeMCU):** G1–G3 OK, **G4: PER < 5.0 % and max RTT < 4.15 ms** over 200 PINGs.

**Expected result.** The NodeMCU baseline (`results/i4_bench/20261028_230328`) was 0.0 % PER and max RTT 0.84 ms. The C3 should land within about 0.1 ms of that, because the RTT is set by the radio, not the MCU. At 2 Mbps:

| Part | Calculation | Time |
|---|---|---|
| TX settling (standby → TX) | datasheet | 130 µs |
| Command frame on air | (1 preamble + 5 address + 32 payload + 2 CRC) × 8 + 9 PCF bits = 329 bits ÷ 2 Mbps | 165 µs |
| RX turnaround at the crane, then ACK with 32-byte payload | 130 µs + 165 µs | 295 µs |
| SPI: write 32 B, read 32 B ACK payload at 4 MHz | 2 × (33 B × 8 ÷ 4 MHz) | 132 µs |
| **Total, no retries** | | **≈ 0.72 ms** |

That is close to the 0.84 ms measured. The extra ~0.1 ms is RF24 library and loop overhead. A C3 result above about 1.5 ms with ARC = 0 would point to a firmware or SPI problem, not the board choice.

Optional, for the report: `--sweep` (PA MIN/LOW/HIGH/MAX). Remember receiver saturation: PA HIGH/MAX at < 2 m makes PER *worse* (Increment 2: 5.5–11 % at PA HIGH, 5 m).

### Step 3: Dashboard

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\dashboard"
python server.py --port COM9 --rig-port COM12 --open
```

| Check | Pass |
|---|---|
| Events log | `Ground station ready: ch 77, 2Mbps, PA LOW, profile ICD` → `Link running` |
| Telemetry | Every Table 34 field updating at 20 Hz; load cell live |
| Short drive session | Trolley / hoist / magnet commands echo back as with the NodeMCU; PER and RTT in the session CSV comparable |
| Lost link | Unplug the C3's USB mid-move → crane `RIG,SAFE_HOLD` within 500 ms. Replug → dashboard reconnects by itself (it retries the port every 2 s) |
| Channel / PA switching | `CH` / `PA` from the UI answered `OK` |

Keep the `results/i4_bench/<run>` and `results/dashboard/<session>` folders as evidence that the spare was verified.

### If something fails

| Symptom | Likely cause |
|---|---|
| No COM port appears | Charge-only cable; or the board is in download mode (press RESET) |
| Monitor blank after flashing | Banner sent before the port opened: press **Enter**. Still blank → wrong env (`_uart` on a native-USB board or the reverse; this was the Increment 2 "silent C3") |
| `radio.begin() FAILED` | VCC not 3.3 V at the module, CSN/CE swapped, MISO/MOSI swapped, a lead not seated |
| Register dump all `0x00` / `0xFF` | MISO (GPIO5) or SCK (GPIO4) |
| `radio=OK` but G4 PER high, or ACKs lost | Missing 10 µF at the module (PA bursts pull the rail down); antennas < 0.5 m apart at PA HIGH/MAX; C3 board touching the antenna |
| Smoke test G2 can't open the port | The PlatformIO monitor is still open |
| Dashboard `check_failed: no ECHO reply` | Wrong `--port`, or the NodeMCU firmware on a different port is being opened |
| Random resets under TX | Weak USB port or hub: plug directly into the laptop |

---

## 5. Pitfalls to avoid

1. **Antenna before power, every time.**
2. **3.3 V only on module VCC.** The C3 board has a 5V pin right next to 3V3 on most layouts.
3. **One program per COM port:** close the monitor before the smoke test, and close the smoke test before the dashboard.
4. **Keep the NodeMCU and C3 on different COM numbers** (COM10 / COM9) and pass the right one with `--port` / `--gs-port`. If both are plugged in, the one the dashboard opens is the ground station.
5. **Don't use GPIO8/9** for anything (LED / BOOT button). If the board won't boot after wiring, check that nothing pulls GPIO2/8/9.
6. **Native USB re-enumerates** when the C3 resets or goes into download mode. A port that disappears for a moment is normal; replug if it doesn't come back.
7. **The baud rate is irrelevant on native USB** (it runs at USB full speed), so a "115200 vs 9600" mismatch can't be the cause of garbled output. Garbled output means the wrong env.
8. **Leave Wi-Fi off.** The C3's own 2.4 GHz radio sits centimetres from the nRF antenna. The C3 can't do the Increment 2 Wi-Fi AP survey; use `SCAN` (nRF RPD) or the NodeMCU `scanner` build for that.
9. **Short SPI leads.** SPI stays at 4 MHz (ICD); only raise it if the module is soldered on.

---

## 6. Paperwork

ICD-001 Rev A Table 31 and locked decision **D1** name the NodeMCU-32S. If the C3 is used at the demo or in the report, add an ICD Rev B note, for example:

> *IF10.0 ground station: ESP32-C3 (native USB-CDC) is an approved alternative to the NodeMCU-32S. nRF pins CE GPIO10, CSN GPIO7, SCK GPIO4, MISO GPIO5, MOSI GPIO6. Radio configuration, serial protocol (IF-E1.0) and Table 33/34 frames unchanged. Verified by smoke test `<run folder>` (PER x %, max RTT y ms).*

Then freeze it with the rest at **v5.0** (G4, Sat 28 Nov, ahead of the 2 Dec demonstration).
