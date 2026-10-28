# 02 — Wiring

Diagrams (draw.io, editable — open in draw.io / diagrams.net or the VS Code Draw.io extension):

| Node | draw.io | PNG |
|------|---------|-----|
| Receiver – NodeMCU-32S | [`wiring/wiring_rx_nodemcu32s.drawio`](wiring/wiring_rx_nodemcu32s.drawio) | ![RX wiring](wiring/wiring_rx_nodemcu32s.png) |
| Transmitter – ESP32-C3 (page 1) / Arduino Nano fallback (page 2) | [`wiring/wiring_tx_esp32c3.drawio`](wiring/wiring_tx_esp32c3.drawio) | ![TX wiring](wiring/wiring_tx_esp32c3.png) ![TX fallback wiring](wiring/wiring_tx_nano_fallback.png) |

The diagrams are generated from the pin tables below by `tools/gen_wiring_drawio.py`; if you change a pin here (and in `firmware/lib/linktest/board_pins.h`), re-run the script and re-export the PNGs.

nRF24L01(+) module header (looking at the component side, header at the top-left, key notch/`GND` square pad):

```
        ┌───────┐
  GND ──┤ 1   2 ├── VCC (3.3 V !)
   CE ──┤ 3   4 ├── CSN
  SCK ──┤ 5   6 ├── MOSI
 MISO ──┤ 7   8 ├── IRQ
        └───────┘
```

All pin numbers below are **GPIO numbers** (what you pass to the Arduino API), not header positions.
Change them in `firmware/lib/linktest/board_pins.h` if your board revision differs.

## NodeMCU-32S (role `rx` / `scanner`)

| nRF24 pin | ESP32 GPIO | Board label | Note |
|-----------|-----------|-------------|------|
| VCC | 3V3 | `3.3V` | plus 10 µF + 100 nF at the module |
| GND | GND | `GND` | |
| CE  | 4  | `P4`  | |
| CSN | 5  | `P5`  | VSPI CS0 (strapping pin; fine as an output, keep it pulled high at boot – the module's CSN is high-Z so this is OK) |
| SCK | 18 | `P18` | VSPI CLK |
| MOSI| 23 | `P23` | VSPI D |
| MISO| 19 | `P19` | VSPI Q |
| IRQ | 17 | `P17` | optional (not required by the firmware) |

Pins to avoid on the NodeMCU-32S: GPIO6–11 (`CLK/SD0–3/CMD` — internal flash), GPIO34–39 (input only), GPIO0/2/12/15 (strapping).

## ESP32-C3 dev board (role `tx`)

| nRF24 pin | ESP32-C3 GPIO | Note |
|-----------|--------------|------|
| VCC | 3V3 | plus 10 µF + 100 nF at the module |
| GND | GND | |
| CE  | 10 | |
| CSN | 7  | FSPICS0 |
| SCK | 4  | FSPICLK |
| MOSI| 6  | FSPID |
| MISO| 5  | FSPIQ |
| IRQ | 3  | optional |

Pins to avoid on the C3: GPIO2/8/9 (strapping, `esp32-c3_datasheet_en.pdf` §2.4), GPIO18/19 (USB-JTAG on boards that use native USB), GPIO12–17 (SPI flash on ESP32-C3 modules), GPIO20/21 (UART0 if you rely on the on-board USB-serial chip).

Power the C3 from a **USB power bank** for the distance tests. Some power banks switch off at < 50 mA load — the TX firmware keeps the radio in Standby-I between bursts, so if your bank cuts out, plug in a small USB "keep-alive" resistor load or use a bank without auto-off.

## Arduino Nano (role `tx`, fallback)

| nRF24 pin | Nano pin | Note |
|-----------|----------|------|
| VCC | **external 3.3 V regulator** fed from `+5V` (pin 27) — **not** the Nano `3V3` pin | see `01_hardware_selection.md` |
| GND | GND | |
| CE  | D9  | |
| CSN | D10 | |
| SCK | D13 | fixed hardware SPI |
| MOSI| D11 | fixed hardware SPI |
| MISO| D12 | fixed hardware SPI |
| IRQ | D2  | optional |

The Nano's IO is 5 V; the nRF24 logic inputs are 5 V tolerant, so no level shifting is needed on CE/CSN/SCK/MOSI.

## Bring-up checklist

1. Antennas screwed on. Module VCC measured at **3.3 V ± 0.1 V** at the module header with the board powered.
2. Flash the role firmware. Open the serial monitor at 115200.
3. Boot banner must show `radio.begin() OK` and `isPVariant=1`. If it shows `radio.begin() FAILED` → SPI/CE/CSN wiring or power.
4. `printPrettyDetails()` output: `RF_SETUP`, `RF_CH`, `EN_AA`, `SETUP_RETR` must read back exactly what the firmware wrote. Garbage / all-0x00 / all-0xFF = MISO or SCK problem.
5. Only then start the smoke test in `03_test_procedure.md`.
