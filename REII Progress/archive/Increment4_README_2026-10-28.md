# REII Increment 4 — crane ↔ ground-station nRF24 link (rebuilt from scratch)

Everything in this folder was written fresh. Nothing was copied from Increment 3, the
"NEWEST main control board firmware" folder or the old Increment 4 attempt. The inputs were:

* **Crane rig:** the provided starter project (`Provided Development Resources/Main Control
  Board/EngDes26_starter_project_ESP-IDF_VSCode.zip`). The radio, console and boot sequence
  were added to it.
* **Radio protocol and ground station:** Increment 2 (`REII_nRF24_Link_Characterisation`).
  That covers the NodeMCU-32S pins, the RF24 setup, the `REII1`/`REII2` addresses, CRC-16,
  32-byte payloads and the line-based serial commands.
* **Settings and pins:** the ICD (ICD v1 IF10.0 Tables 31–34, and the ICD / ESP32-S3 rows of
  `REII Progress/progress_and_milestones.md`). The board nets were checked on `ESP32S3Devboard.pdf`.

```
Increment 4/
├── crane_rig_IDF/          ESP32-S3 crane board (COM12) — ESP-IDF v6.x, starter project + radio
│   ├── main/rig_config.h   ← every pin and link setting lives here
│   ├── main/main.c         starter main.c with the ICD boot sequence added
│   ├── main/nrf24.c/.h     nRF24L01+ register driver (ESP-IDF spi_master, no Arduino)
│   ├── main/rf_link.c/.h   crane radio task: PRX on 'REII2', ACK-payload telemetry, self-tests
│   ├── main/rig_console.c  typed commands on COM12 (USB Serial/JTAG = primary console)
│   ├── main/rig_telemetry.c  fills the Table 34 reply: bench MODEL or ZERO (see "Dashboard")
│   ├── main/loadcell.c     load cell on J6 via the ADS1220 (default) or an HX711 board: tare, calibration, kg x 10
│   └── main/icd_frames.h   ICD Table 33 / 34 32-byte frames (static_assert'ed)
├── groundstation_PIO/      NodeMCU-32S ground station (COM10) — PlatformIO + RF24 1.6.x
│   ├── src/main.cpp
│   └── include/gs_config.h, icd_frames.h
├── dashboard/              operator dashboard: Python backend (terminal) + web UI (browser)
│   ├── server.py           start here: python server.py --port COM10
│   ├── link.py             ground-station link, 20 Hz command loop, deadman, CSV capture
│   ├── icd.py              Table 33 / 34 pack + unpack (python icd.py = self-test)
│   ├── radio.py            power levels + the clean channels from the Increment 2 surveys
│   ├── ramp.py             S-curve (jerk-limited) speed ramp for anti-sway
│   └── web/                index.html, app.js, style.css (no internet needed)
├── tools/
│   ├── smoke_test.py       gates + 200-PING smoke test + RF diagnosis → smoke.csv
│   ├── smoke_test.ps1      PowerShell wrapper
│   └── requirements.txt
└── results/
    ├── i4_bench/<run_id>/      created by smoke_test.py
    └── dashboard/<session>/    telemetry.csv + events.log from every dashboard session
```

## Link settings (ICD v1 IF10.0)

| Setting | Value | Where |
|---|---|---|
| Channel / rate | 77 (2477 MHz) / 2 Mbps — "APP", Normal Crane Control Mode | Table 32 |
| Diagnostic mode | 80 / 250 kbps / ARC 15 / ARD 1500 µs — "CTRL" | Table 28 |
| Auto-ACK / retries | on, ARC 5, ARD 500 µs | Table 32 |
| CRC / addresses | CRC-16, 5 bytes, crane listens on `REII2`, GS on `REII1` | Table 32 |
| Downlink | 32 B, `0xA5`, `msg_type` 1 = ping / 2 = move, 8-bit `sequence` | Table 33 |
| Uplink | 32 B telemetry `0x81` inside the auto-ACK (ACK payload) | Table 34 |
| PA on the bench | LOW (PA_MAX saturates the receivers under 2 m) | Table 29 |
| Alternative channels | 75–81 and 2–4, clean in all nine Increment 2 surveys; switchable on the dashboard | `dashboard/radio.py` |

**Two payload profiles.** Both ends must use the same one. `smoke_test.py` sets both.

* `ICD` (boot default): dynamic payload length plus a 32-byte ACK payload, so every ACK carries
  the 0x81 telemetry frame. This is Normal Crane Control Mode as the ICD specifies it.
* `STATIC`: fixed 32-byte payloads with a plain auto-ACK. This is the setup of the passing
  16 Oct smoke test (ICD §5.5.2).

A mismatch between these is enough to give the "0 frames on the crane" symptom: ACK
payloads and dynamic length need `EN_DPL` on both radios. The tool prints and checks both
ends' configuration before it sends anything.

## Crane wiring (ICD Table 31; checked on the schematic)

| nRF pin | Crane board | ESP32-S3 |
|---|---|---|
| 1 GND | J1.5 / J1.6 | — |
| 2 VCC | **J1.8 +3.3VP** (never J1.7 = 5 V), 10 µF + 100 nF at the module | gated by GPIO47 |
| 3 CE | **J3.3** | MCP23017 GPA0, via I2C 0x20 |
| 4 CSN | J1.1 SPI_xCS | GPIO17 |
| 5 SCK | J1.3 | GPIO12 |
| 6 MOSI | J1.2 | GPIO11 |
| 7 MISO | J1.4 | GPIO13 |
| 8 IRQ | not connected (STATUS is polled every 1 ms) | — |

**CE is on J3.3 only** (ICD v1 Table 31, `RIG_CE_PATHS` in `rig_config.h`). J3.3 is the
MCP23017's GPA0, so CE depends on the expander: the firmware releases its `/RESET` (GPIO5,
PX_RESET) at boot, sets GPA0 as an output and drives it high. Every heartbeat (5 s) it reads
J3.3 back and re-drives it if it has dropped (`RIG,WARN,CE on J3.3 ...`). If the MCP23017 does
not answer on I2C, the boot log says `RIG,ERR,CE path J3.3 unavailable` and the radio cannot
receive. J7.10 is no longer used; take the CE wire off it.

Ground station (NodeMCU-32S): CE GPIO4, CSN GPIO5, SCK GPIO18, MISO GPIO19, MOSI GPIO23, 3V3, GND.

## 1. Flash the crane (COM12)

1. VS Code → **File → Open Folder** → `Increment 4\crane_rig_IDF`. Open this folder on its own,
   not the whole repo.
2. In the ESP-IDF status bar, set target **esp32s3** and port **COM12**. The `.vscode` folder
   is not included, so you have to set these once yourself.
3. **Full clean** first. This matters because `sdkconfig.defaults` changed: it adds the USB
   console and the 1 ms tick. Then **Build → Flash → Monitor**. From an ESP-IDF terminal:
   `idf.py fullclean`, `idf.py set-target esp32s3`, then `idf.py -p COM12 build flash monitor`.

Expected boot (abridged):

```
I (...) main: REII crane rig (Increment 4) v4.4.1 on esp32s3
I (...) console: typed commands enabled on the console port - type HELP
I (...) main:   found device at 0x20 / 0x38 / 0x51
I (...) main: MCP23017 at 0x20 responding with sane register defaults
I (...) main: ADS1220 responding with sane register defaults
I (...) loadcell: load cell on J6 (AIN0-AIN1, gain 128, 20 SPS, 50 Hz reject), datasheet scale ..., taring now - keep the hook unloaded
I (...) rf_link: probe OK: SPI read-back good, SETUP_AW=0x03, nRF24L01+
RIG,BOOT,profile=ICD,mode=APP,ch=77,rate=2M,pa=LOW(-12dBm),arc=5,ard_us=500,...,ce_paths=J3.3(MCP GPA0),ce_j7_10=-1,ce_j3_3=1,cfg_errors=0
RIG,REGS,CONFIG=0F,EN_AA=03,EN_RXADDR=02,SETUP_AW=03,SETUP_RETR=15,RF_CH=77,RF_SETUP=0A,...,DYNPD=03,FEATURE=06
RIG,ADDR,RX_ADDR_P1=0x3249494552('REII2'),TX_ADDR=0x3149494552,...
I (...) rf_link: link up: PRX on 'REII2' ch77/2M dyn32+ACK-payload telemetry (ICD), listening
RIG,HB,up=5,frames=0,pings=0,...,link=LOST,profile=ICD,mode=APP,ch=77,pa=LOW,status=0E,fifo=11,config=0F,ce_j7_10=-1,ce_j3_3=1   (every 5 s)
RIG,LOAD_TARE,done
```

✅ **G1:** `probe OK`, `cfg_errors=0`, `CONFIG=0F`, `RF_CH=77`, `ce_...=1`, and no `RIG,ERR` lines.
If you see `nRF24 NOT RESPONDING`, the problem is SPI or power on J1, not RF.

Typing works in the monitor now, because COM12 is the primary console: `HELP`, `INFO`,
`STATS`, `PROFILE ICD|STATIC`, `MODE APP|CTRL`, `PA MIN|LOW|HIGH|MAX`, `CH n`, `SCAN`,
`RPD`, `CARRIER`, `CE 0|1`, `REINIT`, `SELFTEST`. The **BOOT** button runs `SELFTEST`, which
does a receiver scan and then a 5 s carrier.

## 2. Flash the ground station (COM10)

Open `Increment 4\groundstation_PIO` in VS Code with PlatformIO, then **Upload → Monitor**.

```
# REII groundstation v4.1.0 board=NodeMCU-32S app_ch=77 ctrl_ch=80 (Increment 4)
radio.begin() OK, isPVariant=1
 ... Channel = 77, RF Data Rate = 2 MBPS, CRC Length = 16 bits, Dynamic Payloads = Enabled, Custom ACK Payload = Enabled
GS,LINK,mode=APP,ch=77,rate=2M,pa=LOW,arc=5,ard_us=500,profile=ICD,payload=dyn32+ackpl32,tx_to=REII2,radio=OK
OK,app_ch=77,2M,PA_LOW,ARC5,ARD500
READY
```

✅ **G2:** `radio.begin() OK`, `isPVariant=1`, ch 77, 2 Mbps.

Quick manual check: type `PING` a few times. You should see `PONG,1,...` and
`TLM,32,81...` on the GS, and `RIG,RX,<n>,1,<seq>,32,1,0` on the crane.

## 3. Smoke test → `smoke.csv`

Close **both** serial monitors, because Windows lets only one program own a port. Then run:

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\tools"
python -m pip install -r requirements.txt
python smoke_test.py --gs-port COM10 --rig-port COM12                 # ICD profile, 200 PINGs, PA LOW
python smoke_test.py --gs-port COM10 --rig-port COM12 --profile both  # STATIC bring-up profile first, then ICD
python smoke_test.py --gs-port COM10 --rig-port COM12 --sweep         # PA MIN/LOW/HIGH/MAX
python smoke_test.py --gs-port COM10 --rig-port COM12 --diag-only     # "which radio is deaf / mute?"
```

The `.\smoke_test.ps1` wrapper takes the same options (`-Profile both`, `-Sweep`, `-DiagOnly`).

What it does:

1. **G2** resets the GS and checks its radio.
2. **G1** asks the crane for `INFO` and checks the probe, register read-back, CE level and
   address. It does not reset the crane.
3. **G3** sets both ends to the same profile, APP mode and PA, then reads both back.
4. **G4** sends 200 × `PING` at 20 Hz (ICD: 20–50 updates/s). It records the GS ACK, the RTT,
   the retries and the ACK-payload telemetry. It matches every round against the crane's
   `RIG,RX` log. Pass: **PER < 5.0 %** and **max RTT < 4.15 ms**.
5. If a run gets no link at all, it runs the RF diagnosis automatically:
   * receiver scans on both radios;
   * crane carrier → GS RPD, then GS carrier → crane RPD.

   The findings say which radio, or which setting, is at fault.

Output is written to `results/i4_bench/<run_id>/`:

| File | Content |
|---|---|
| `smoke.csv` | `run_id, profile, gs_pa, rig_pa, round, seq, ack, rig_rx, rtt_ms, rtt_us, arc, tlm_len, tlm_magic, tlm_seq_ack, tlm_health, tlm_fault, tlm_ok, timestamp` |
| `sweep.csv` | one summary row per profile/PA: PER, crane received, ACK lost, never arrived, RTT min/avg/max, retries, valid telemetry |
| `SUMMARY.md`, `summary.json` | PASS/FAIL per gate with the evidence lines |
| `groundstation.log`, `rig.log` | full timestamped captures of both ports (commands marked `>>>`) |
| `diagnosis.json` | only if the RF diagnosis ran |
| `smoke.png` | RTT per round + cumulative ACKed vs crane-received (needs matplotlib) |

`rig_rx` separates "never arrived" from "arrived but the ACK was lost". The latter is the
PA_MAX-under-1 m case from 16 Oct.

`tlm_seq_ack` normally equals the **previous** round's seq. The crane loads the ACK payload
before the next command arrives, so each ACK answers the command before it. The tool counts
this.

## Cross-check: is it the crane hardware or the crane firmware?

`groundstation_PIO` has a second build target, **`crane_rf24_check`**. It is not the crane
firmware. It runs the ground station's RF24 code on the **ESP32-S3 crane board** (COM12),
using the crane's wiring: J1 SPI, CSN GPIO17, +3.3VP on GPIO47, but CE on J7.10 (GPIO10),
because the RF24 library can't drive CE through the MCP23017. For this check only, move the CE
wire from J3.3 to J7.10 (or strap CE to J1.8 +3.3VP), and put it back on J3.3 afterwards. It
listens as the crane on `REII2`.

1. In VS Code / PlatformIO, open `groundstation_PIO` and pick the environment
   **`env:crane_rf24_check`** in the bottom status bar. Then **Upload** (COM12) and
   **Monitor**.
2. In the crane monitor, type `SCAN`. The receiver works if you see `SCAN_END,total_hits=` > 0
   with hits in the Wi-Fi channels.
3. Close the crane monitor and run the ground-station-only smoke test:
   `python smoke_test.py --gs-port COM10 --profile icd`
   (or type `PING` on the GS and watch `RIG,RX` lines on the crane).

How to read the result:

| RF24 on the crane | Meaning |
|---|---|
| hears Wi-Fi, PINGs ACKed | crane hardware is fine, so the fault is in `crane_rig_IDF`. Send the logs |
| still deaf | crane board / harness fault, independent of any firmware |

When you are done, re-flash `crane_rig_IDF` to put the real crane firmware back.

## 4. Dashboard (Python backend in a terminal + web page)

Flash both boards with the current firmware first: crane **v4.4.1**, ground station **v4.1.0**.
The smoke test must pass before you start. Then:

```powershell
cd "D:\DESIGN GIT\Design-Material\Increment 4\dashboard"
python -m pip install -r requirements.txt
python server.py --port COM10                     # ground station only
python server.py --port COM10 --rig-port COM12    # also read the crane's console: live load cross-check, Tare / Calibrate
```

Open **http://localhost:5000** in a browser. Add `--open` to have it open automatically. Stop
with **Ctrl+C**. Only one program can hold COM10, so close the smoke test and any monitors
first.

What happens, following the ICD IF10.0 operating sequence:

1. **USB check mode.** `ECHO` round trip over the cable.
2. **Set up.** `APP` (Normal Crane Control Mode, 2 Mbps), `PROFILE ICD`, `PA LOW`, `CH 77`.
   Then `INFO` must report that channel, 2M and radio OK. With `--rig-port`, the crane is also
   set to the same channel and power over USB.
3. **Control loop.** It runs at **20 Hz** (ICD: 20–50 updates/s, `--rate`). Each cycle packs a
   Table 33 command from your controls and sends `CMD <hex>`. The crane's auto-ACK brings back
   the Table 34 telemetry, which the page decodes.

What the page shows:

| Area | What it is |
|---|---|
| Gantry drawing | Trolley position along the rail, hook height, magnet on/off, load, rail end-stops (red on a rail limit) |
| Readouts | Every Table 34 field in engineering units (m, m/s, mm/s, kg, A) |
| Faults / crane status | The 8 `fault_flags` bits, `error_code` and the 5 `system_health` bits |
| Last frames on air | The 32 bytes of the last command and the last reply, coloured by ICD field. Hover a byte to see its field |
| Link bar | Loss over the last 100 commands, round trip, retries, radio power and channel |
| Last 30 seconds | Trolley position, hook height and load |
| Event log | Link state, faults raised and cleared, deadman, soft stop, crane warnings |

Operator controls:

* **Trolley / Hoist sliders.** You set a target speed (−100…+100 %). The command actually sent
  ("Sent to crane") follows it through the **S-curve ramp** (`ramp.py`: 100 %/s acceleration,
  400 %/s² jerk; change with `--accel` / `--jerk`). This is the anti-sway baseline from D-04.
  While ramping, `segment_idx` counts the steps.
* **Return to zero when released** (on by default): the slider springs back to 0 when you let go.
* **Keyboard:** ← → trolley, ↑ ↓ hoist (60 % while held), **M** magnet, **Space** soft stop.
* **Energise magnet, Hold hoist brake, Auto mode** set Table 33 bytes 6 and 3 (bits b4, b2).
* **Reset faults, Zero position** send the b1 / b3 flag for 5 commands.
* **Ping crane** sends one `msg_type 1` frame.
* **Radio link** box (see below): power LOW / HIGH and the channel.
* **Soft stop** sets b0 and zeroes both speeds at once. It stays latched until you press
  **Release soft stop**, and speed requests are ignored while it is latched. It is a software
  command; the red E-stop (PHY2) is the safety stop.

### Radio link: power and channel

Both settings are applied between two commands, so the link only pauses for a few
milliseconds. The line under the two selectors shows what each end is actually set to.

* **Power: Low (−12 dBm) or High (−6 dBm).** The ground station changes over its serial
  port. With `--rig-port COM12`, the crane changes too, over USB; its power is the one the
  telemetry comes back at. Without `--rig-port` only the ground station changes. MIN and MAX
  are still available from the command line (`--pa MAX` for the rail run).
* **Channel: the clean channels from Increment 2.** These are the channels whose RPD
  occupancy stayed ≤ 3 % with no WiFi overlap in **all nine** lab surveys (7–8 Sep), grouped as
  **75–81** (the gap above WiFi ch 11; 77 is the ICD default) and **2–4** (below WiFi ch 1).
  Hover an entry to see its worst measured occupancy. The list and its sources are in
  `dashboard/radio.py`.
  * Needs `--rig-port COM12`: for now the crane can only be told over USB (`CH n`).
  * The trolley and hoist must be at rest.
  * The crane switches first, then the ground station. If no ACK comes back within 6 commands
    (0.3 s), both ends go back to the previous channel by themselves. If the crane shows a
    Wireless timeout fault afterwards, press **Reset faults**.
  * The dashboard is the reference. If the crane reboots (back to 77, LOW) it is set to the
    dashboard's channel and power again. When you stop the dashboard with Ctrl+C, the crane is
    put back on channel 77, so the smoke test finds it where it expects.
  * WiFi ch 13 (legal in South Africa) would cover nRF 61–83 and was not present in the
    surveys. In a different room, re-run the Increment 2 survey first.

Safety behaviour built in:

* **Deadman.** If the page stops sending its heartbeat for 1 s (tab closed, PC asleep, network
  gone), both targets go to 0 and the ramp stops the crane. The page shows a banner.
* **Lost link.** After 10 commands in a row without an ACK, the link shows **No reply from
  crane**. The crane's own 500 ms timer puts it in safe hold: speeds 0, brake clamped, magnet
  kept on (ICD). It latches the Wireless timeout fault until you press **Reset faults**.
* **Dashboard closed.** Commands stop, so the crane enters safe hold by itself.

Evidence: every session writes `results/dashboard/<session>/telemetry.csv`. That's one row per
command: sequence, ACK, RTT, retries, every Table 33 field, every decoded Table 34 field and both
raw hex frames. It also writes `events.log`. This is the timestamped FR12/FR13 capture the
progress record asks for.

### Where the telemetry comes from (crane `TLM` command)

The crane fills Table 34 from two kinds of source.

* **The load cell (bytes 10–11) is always real.** It's measured by the ADS1220 on J6 (see "Load cell"
  below), in both modes. The > 20.0 kg Overload fault and the lift inhibit use the real weight.
* **The other fields** (positions, speeds, currents, brake/VFD flags) come from one of two
  sources. Switch on COM12 with `TLM MODEL` / `TLM ZERO`; the boot default is
  `RIG_TLM_DEFAULT_MODEL` in `rig_config.h`.
  * **MODEL** (Increment 4 default) is a bench loopback model driven by your commands, which
    proves every downlink field reaches an uplink field.
    * Trolley speed = command × 1 m/s (SysRS PT1). Hoist speed = command × 80 mm/s (PT2).
    * Positions integrate. The rail (5 m placeholder) and the 1.6 m stroke clamp, with a rail
      limit fault at the ends.
    * Magnet current is 7.5 A when on. The brake clamps whenever the hoist isn't moving.
    * **These values are not measured.**
  * **ZERO** sends only link status and the load cell. Everything else is 0.

The model's placeholder numbers are in `rig_config.h` (`RIG_MODEL_*`). As each subsystem is
integrated, its real reading replaces the model value in `rig_tlm_build()`, the way the load
cell already has.

## 5. Load cell (ICD IF13.0)

### Wiring: load cell straight onto J6 (the default, ICD Table 38)

No amplifier board. The crane board's own ADS1220 (24-bit, PGA up to 128) does the job of the
HX711. Disconnect the HX711 board and wire the four load-cell wires to J6:

| Load cell wire (usual colour) | J6 pin | Goes to |
|---|---|---|
| EXC+ / E+ (red) | **J6.1** | +3.3VP excitation (also the ADC reference) |
| EXC− / E− (black) | **J6.2** | GND |
| SIG+ / A+ (green) | **J6.5** | ADS1220 AIN0 |
| SIG− / A− (white) | **J6.6** | ADS1220 AIN1 |

Colours vary between cells. If the datasheet disagrees, follow the datasheet. A shield wire,
if there is one, goes to GND only at the crane end. Nothing else is plugged into J6.

How the firmware reads it (`loadcell.c`, `ads1220.c`):

* AIN0 − AIN1 at gain 128, 20 samples/s, continuous conversion, DRDY on GPIO15, CS on GPIO14.
* The reference is the excitation itself (AVDD), so the reading is ratiometric: slow drift on
  +3.3VP cancels out.
* The 50 Hz FIR filter rejects mains hum on the cell leads.
* A 4-sample average gives a 0.2 s response. TARE / CAL average 1 s.
* Full scale is ±7.8 mV/V. A 2 mV/V cell uses about a quarter of that, so a crossed or open
  signal wire shows up as `saturated=1` rather than a plausible weight.

### Alternative: HX711 amplifier board

If the HX711 board is used instead, set `RIG_LOADCELL_SOURCE RIG_LOADCELL_HX711` in
`rig_config.h`, rebuild, and wire its header as follows. **Never VCC to J3.2: that pin is +5 V.**

| HX711 pin | Crane board | ESP32-S3 |
|---|---|---|
| VCC | J3.11 | +3.3VP |
| GND | J3.1 | GND |
| DAT | J7.11 | GPIO4 |
| CLK | J7.4 | GPIO42 |

CLK and DAT must be direct ESP32 pins, not J3.7 / J3.8 as ICD Table 38 lists. Those pins sit
behind the MCP23017, which is too slow: the HX711 powers down if CLK stays high for more than
60 µs. If this option is ever used, record the change in the ICD (D-06).

### Common to both

* kg = (reading − tare) × scale, sent as **kg × 10** in Table 34 bytes 10–11 (e.g. 185 = 18.5 kg),
  so the radio value has 0.1 kg resolution. The crane's USB line has 0.01 kg.
* **Tare** is taken automatically just after boot (ICD start-up sequence), so boot with the
  hook unloaded.
* **Scale** starts from `RIG_LOADCELL_RATED_KG` / `RIG_LOADCELL_SENS_MV_V` in `rig_config.h`
  (20 kg, 2.0 mV/V). Calibrate once with a known mass and it is exact. The scale is saved in
  flash and survives reflashing the same app.
* If the reading goes *down* when you add weight, calibrating fixes the sign by itself; there's
  no need to swap wires.
* If the reading hits the converter's limit, the crane sends 0xFFFF. The dashboard shows
  **over range**, and it counts as an overload, so lifting is inhibited.

### Test: live load-cell feedback on the dashboard

1. **Wire:** CE on **J3.3**, load cell on **J6** as above. Flash the crane (**v4.4.1**) and boot
   it with **nothing on the hook**.
2. **Check the boot log** (COM12 monitor):
   * `MCP23017 responding` and no `RIG,ERR,CE path J3.3 unavailable`;
   * `load cell on J6 (AIN0-AIN1, gain 128, 20 SPS, 50 Hz reject) ... taring now`, then
     `RIG,LOAD_TARE,done`;
   * `INFO` shows `ce_paths=J3.3(MCP GPA0),ce_j7_10=-1,ce_j3_3=1`.
3. **Quick look in the monitor:** type `LOAD WATCH 10`. Unloaded you should see
   `src=ADS1220,valid=1,saturated=0` and `kg` near 0.00. Press gently on the cell and `kg`
   rises. `saturated=1` means a J6 wire is open or crossed.
4. **Radio check:** close the monitor and run the smoke test. G1 should report
   `CE high (J7.10=-1, J3.3=1, paths J3.3(MCP GPA0))`.
5. **Dashboard:** in `dashboard\`, run

   ```
   python server.py --port COM10 --rig-port COM12 --open
   ```

   `--rig-port` is what turns on the live cross-check and the Tare / Calibrate buttons. The
   dashboard keeps the crane streaming its own reading at 5 Hz (it re-sends `LOAD WATCH`), so
   both values update live.
6. **Under Load** you now see:
   * the big number, the bar and the hook box: the radio value (Table 34), updated 20 times a
     second;
   * the green line *"Crane (ADS1220) reads 0.00 kg over USB ... Radio value matches"*;
   * **Tare**, a box for a known mass, and **Calibrate**.
7. **Calibrate from the page:** with the hook empty press **Tare**. Hang a known mass (for
   example 5.000 kg, weighed on a kitchen scale), type `5` in the box and press **Calibrate**.
   The event log shows *"load cal done"* about 2 s later. Then press on, or swap, masses and
   watch the number, the bar and the chart's Load trace follow. Green means the radio and USB
   paths agree to within 0.15 kg.
8. **Evidence:** `results/dashboard/<session>/telemetry.csv` holds the radio value in its
   `load_kg` column for every command, and `events.log` records the tare / calibration.
9. Optional: test the overload fault without a 20 kg mass. Pressing the cell by hand past
   20 kg lights **Overload (> 20 kg)**, and hoist up is then inhibited until the load drops back
   under 20.0 kg.

Console commands (COM12):

| Command | What it does |
|---|---|
| `LOAD` | One line: raw code, mV/V, kg, tare, scale, calibrated, samples, errors |
| `LOAD WATCH [s]` | The same line at 5 Hz for s seconds (default 10) |
| `LOAD TARE` | Re-zero with the hook unloaded (1 s average) |
| `LOAD CAL <kg>` | Calibrate with a known mass hanging (1 s average); saved in flash |
| `LOAD RESET` | Back to the `rig_config.h` label scale |

## What this firmware deliberately does not do

* It drives no actuators. `MOVE` frames (type 2) only feed the telemetry model and are logged
  when they change (`RIG,CMD,...`). Motor, brake and magnet outputs come with the MCTR/EEII
  integration.
* The lost-link timeout uses a 500 ms placeholder. The ICD still has the freshness value as
  TBD (Q10).
* SD card and SPWM are off (`RIG_ENABLE_SDCARD` / `RIG_ENABLE_SPWM` in `rig_config.h`). AHT20
  still logs every 5 s. The ADS1220 is dedicated to the load cell on J6.

## Next steps

Change one thing at a time, and re-run the smoke test and a short dashboard session after each:

1. Replace model fields with real readings, one interface at a time (load cell done):
   * encoder → `trolley_pos` / `trolley_vel`
   * current sensors → `vfd_current` / `magnet_current`
2. Drive the real outputs from the Table 33 fields once each owner has signed off its byte
   (D-01).
3. Do the rail run at PA_MAX (`--pa MAX` on the dashboard, `PA MAX` on the crane).
4. **Switch channel over the air**, so the dashboard no longer needs the crane's USB cable to
   change channel. A proposal to put to the ICD owners (needs a Table 33 change, D-01):
   * a new `msg_type 3` (radio config) frame carrying the new channel and a switch-at sequence
     number, sent several times;
   * the crane ACKs it, then both ends switch when that sequence number is reached;
   * if the crane hears nothing on the new channel for 1 s, it returns to 77 by itself, and the
     ground station does the same, so a failed switch can never strand the crane;
   * the dashboard keeps the same dropdown and only changes how the crane is told
     (`_set_channel()` in `link.py`).
5. Fit a ZV shaper on top of the S-curve once the sway frequency is measured (D-04).
