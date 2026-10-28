# REII ESP Groundstation — NodeMCU-32S (D-08; STM32 superseded D-09)

> PlatformIO project. Open THIS folder in VS Code (PlatformIO IDE) to build/flash.
> Bench rig under `project_sources/02_increment_2/rf_characterisation/firmware/` is untouched.

## Wire (before power)

- nRF24 VCC → 3V3 (3.3 V only), GND → GND, CE → GPIO4, CSN → GPIO5, SCK → GPIO18, MOSI → GPIO23, MISO → GPIO19.
- 10 uF + 100 nF across VCC/GND at the module. Antenna on before power. Dupont SPI ≤ 4 MHz (set in `src/groundstation.h`).
- Source: `project_sources/02_increment_2/rf_characterisation/docs/02_wiring.md` + `board_pins.h` (BOARD_NODEMCU32S).

## Flash

1. Install the PlatformIO IDE extension in VS Code.
2. File → Open Folder → this `groundstation_esp` folder.
3. Select env `groundstation_nodemcu32s`, Upload (→), then Serial Monitor (115200).

## Bring-up order (capture each step to `results/i3_bench/`)

1. `ECHO hello` → `ECHO,hello` (proves USB-CDC before touching RF).
2. `INFO` → `radio.begin() OK, isPVariant=1` + `printPrettyDetails` with RF_CH/RF_SETUP/SETUP_RETR as set.
3. `PING` → `PONG,1,<ms>` against the second nRF end (bench TX peer on `REII2`, control ch 80).
4. `APP` → `OK,app_ch=77,2M,PA_MAX,ARC5,ARD500`, then `run_test.py --n 200` smoke at ch 77 (PA_LOW below ~2 m).
5. `CMD <64 hex>` → `ACK,1,payload=...` loopback once the Sec 4 byte map is frozen (currently PROVISIONAL per `docs/05_protocol.md`).

## Files

- `platformio.ini` — env `groundstation_nodemcu32s` (`nodemcu-32s`, Arduino, RF24 1.4.9).
- `src/groundstation.h/.cpp` — pins + app/control RF init tables.
- `src/main.cpp` — USB-CDC bridge (`INFO/PING/APP/CTRL/CMD/ECHO/HELP`).
