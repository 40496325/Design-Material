# 01 — Hardware selection

## Decision

| Node | Board | Why |
|------|-------|-----|
| **RX / ground-station stand-in / channel scanner** | **NodeMCU-32S** (ESP32-WROOM-32) | Sits on the bench, USB to the PC. On-board 3V3 LDO is rated for the ESP32's own >500 mA peaks (`nodemcu-32s_product_specification.pdf`, Table 1: "Current > 500 mA"), so it can also feed a PA+LNA module. Its WiFi radio doubles as the **WiFi survey instrument** for the channel study. Hardware VSPI (GPIO18/19/23). |
| **TX / mobile node** | **ESP32-C3 dev board** | Small, runs from a USB power bank, GPIO-matrix SPI so pins are flexible. Same Arduino/RF24 code as the NodeMCU. |
| **Fallback TX** | **Arduino Nano** | Only if the C3 board misbehaves. See power warning below. |

Both ESP32 boards run **WiFi and BT switched off** in the `tx`/`rx` roles (`WiFi.mode(WIFI_OFF); btStop();`) — the ESP32's own 2.4 GHz radio a few cm from the nRF24 antenna would corrupt the measurements. WiFi is enabled only in the `scanner` role, and only for the duration of the AP scan.

## Why not STM32F411 yet

The checkpoint explicitly asks for an *existing dev board / breadboard*. The STM32 dongle needs USB-CDC + SPI + nRF driver from scratch; doing the radio characterisation on Arduino-framework boards with the mature [RF24](https://github.com/nRF24/RF24) library removes the driver as a variable. The recommended register configuration that comes out of this study is what the STM32 driver will then be written to.

## PA+LNA module — power budget and pitfalls

The two modules are the large variant with external antenna: nRF24L01+ die + RFX2401C PA/LNA front end.

| Item | Value | Source / note |
|------|-------|---------------|
| nRF24L01 supply | 1.9 – 3.6 V | `NRF24L01.PDF` §1 features. **3.3 V only — 5 V destroys it.** Logic pins are 5 V tolerant, VDD is not. |
| nRF24L01 TX current @ 0 dBm | 11.3 mA | `NRF24L01.PDF` §1 |
| nRF24L01 RX current | ~12 mA | `NRF24L01.PDF` Table 9 |
| PA+LNA module TX peak (RFX2401C at +20 dBm) | ~115–130 mA bursts | Front-end vendor data, not in our datasheet folder — treat as ≥ 150 mA design figure. |
| Start-up from power-down | 1.5 ms | `NRF24L01.PDF` §6.1.7 |
| Standby → TX/RX | 130 µs | `NRF24L01.PDF` Table 13 (T_stby2a) |

Consequences:

1. **Decouple at the module**: 10 µF (electrolytic or tantalum) **+** 100 nF ceramic across VCC/GND on the module header, as close to the pins as possible (`NRF24L01.PDF` §10.4 asks for RF-grade decoupling close to VDD). The PA current bursts otherwise pull the 3V3 rail down and the radio resets / loses ACKs — the #1 reported failure mode of these modules.
2. **Arduino Nano 3V3 pin is NOT usable.** `ARDUINONANO.PDF` Table: pin 17 `3V3` = "+3.3 V output (from FTDI)". The FTDI/CH340 internal regulator supplies ~50 mA. If the Nano is used, feed the module from a separate AMS1117-3.3 (the common "nRF24 socket adapter" board does exactly this from the Nano's 5 V pin) or a bench supply.
3. **Receiver saturation at short range.** With both modules at `PA_MAX` and < ~1 m apart the LNA of the receiving module is overdriven and the link *gets worse*. For bench bring-up use `PA_LOW` or `PA_MIN`, and never put antennas closer than ~0.5 m. The test procedure keeps 1 m as the first distance point and records PA_LOW there as a control.
4. **SPI wiring length.** RF24 defaults to 10 MHz SPI. Over Dupont leads on a breadboard use ≤ 4 MHz (`RF24 radio(CE, CSN, 4000000)`) — set in `firmware/lib/linktest/board_pins.h`.
5. **Antennas** vertical, same polarisation on both ends, 2.4 GHz SMA "duck" antennas screwed on before power-up (transmitting into no antenna can damage the PA).
6. Optional but effective: wrap the module (not the antenna) in insulating tape and then aluminium foil connected to GND to reduce ESP32/USB noise pickup.

## Things the datasheets confirm we can rely on

* Channel formula `F0 = 2400 + RF_CH MHz`, 126 channels 0–125, bandwidth 1 MHz @1 Mbps, 2 MHz @2 Mbps (§6.3). ⇒ neighbouring groups must be ≥ 2 channels apart at 2 Mbps.
* `OBSERVE_TX`: `ARC_CNT` (retransmits of current packet) and `PLOS_CNT` (lost packets since last `RF_CH` write, saturates at 15) — "can be used for an overall assessment of channel quality" (§7.5.2). The TX role reads these after every packet to build the retry statistics.
* `CD` (carrier detect, register 0x09; called `RPD` on the +) goes high after ≥ 128 µs of RF present in RX mode (§6.1.3). The scanner dwells ≥ 200 µs per channel sample so that the bit is valid.
* Enhanced ShockBurst™ ACK timing: `ARD` must be ≥ 500 µs for full-payload ACKs at 1 Mbps (§7.5.2 note). The + at 250 kbps needs ≥ 1500 µs (RF24 library documentation). We use `ARD = 1500 µs` everywhere so the same value is valid for all rates.
