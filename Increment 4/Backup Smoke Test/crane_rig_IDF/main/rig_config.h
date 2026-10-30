// =============================================================================
// rig_config.h - every pin and radio setting the crane rig firmware uses
// =============================================================================
//
// Source of truth: ICD v1 (Group 13 report) IF10.0, Tables 31-34, and the REII
// progress record (progress_and_milestones.md, "Interface Control Document" +
// "ESP32-S3 controller RF integration" rows). Board nets checked against
// ESP32S3Devboard.pdf (J1 "Comms Exander", J3 EXP_PortA, J7, U4 MCP23017).
//
// Change things HERE only; nrf24.c / rf_link.c / main.c take everything from
// this file.
// =============================================================================
#pragma once

#include <stdint.h>

#define RIG_FW_VERSION "4.0.0"
#define RIG_FW_NAME    "REII crane rig (Increment 4)"

// -----------------------------------------------------------------------------
// Board power / shared buses (from the starter project, unchanged)
// -----------------------------------------------------------------------------
#define RIG_PERIPH_PWR_EN_GPIO 47   // PWR_EN_3V3P -> +3.3VP rail (feeds J1.8 = nRF VCC)
#define RIG_STATUS_LED_GPIO    48   // STAT_LED -> WS2812B (confirmed on schematic)
#define RIG_BOOT_BUTTON_GPIO   0    // ESP_BOOT button, pressed = LOW -> runs the radio self-test

#define RIG_I2C_SDA_GPIO 8
#define RIG_I2C_SCL_GPIO 9

#define RIG_SPI_HOST      SPI2_HOST
#define RIG_SPI_SCK_GPIO  12        // J1.3 SPI_CLK
#define RIG_SPI_MOSI_GPIO 11        // J1.2 SPI_MOSI
#define RIG_SPI_MISO_GPIO 13        // J1.4 SPI_MISO
#define RIG_ADC_CS_GPIO   14        // ADS1220 /CS (on-board)
#define RIG_ADC_DRDY_GPIO 15
#define RIG_MMC_CS_GPIO   2         // SD card CS (on-board)

// MCP23017 U4 (I2C 0x20). Its /RESET is net PX_RESET = GPIO5 (10k pull-up to
// +3.3VP). We drive it explicitly so the expander is always out of reset.
#define RIG_MCP_RESET_GPIO 5

// -----------------------------------------------------------------------------
// nRF24L01+ wiring (ICD v1 Table 31)
// -----------------------------------------------------------------------------
//   nRF pin 1 GND  -> J1.5 / J1.6
//   nRF pin 2 VCC  -> J1.8 +3.3VP   (NEVER J1.7 = +5 V), 10 uF + 100 nF at the module
//   nRF pin 3 CE   -> J3.3 = MCP23017 GPA0          (ICD v1 Table 31)
//                     J7.10 = GPIO10 (IO_B)          (progress record, GPIO lock-in 2026-10-21)
//   nRF pin 4 CSN  -> J1.1 SPI_xCS = GPIO17
//   nRF pin 5 SCK  -> J1.3 = GPIO12
//   nRF pin 6 MOSI -> J1.2 = GPIO11
//   nRF pin 7 MISO -> J1.4 = GPIO13
//   nRF pin 8 IRQ  -> not connected (STATUS is polled over SPI)
#define RIG_NRF_CSN_GPIO  17
#define RIG_NRF_SPI_HZ    4000000   // ICD: SPI capped at 4 MHz
#define RIG_NRF_CE_GPIO   10        // J7.10
#define RIG_NRF_CE_MCP_BIT 0        // GPA0 = J3.3

// The two project documents disagree on where CE lands (J3.3 vs J7.10). For
// bench bring-up the firmware drives CE on BOTH, so the radio works whichever
// header the CE jumper is actually on. Once the harness is fixed, set this to
// the single path the ICD freezes.
#define RIG_CE_PATH_MCP_J3_3   0x01
#define RIG_CE_PATH_GPIO_J7_10 0x02
#define RIG_CE_PATHS (RIG_CE_PATH_MCP_J3_3 | RIG_CE_PATH_GPIO_J7_10)

// -----------------------------------------------------------------------------
// RF link (ICD v1 IF10.0 Table 32 / progress record D-00a, D-00c)
// -----------------------------------------------------------------------------
// Addresses are written to the radio in this byte order (same as the RF24
// library on the ground station), so they print as 0x3249494552 / 0x3149494552.
#define RIG_ADDR_CRANE      {'R', 'E', 'I', 'I', '2'}   // crane listens here (pipe 1)
#define RIG_ADDR_GS         {'R', 'E', 'I', 'I', '1'}   // ground station listens here
#define RIG_PAYLOAD_BYTES   32

// Normal Crane Control Mode ("APP"): ch 77 / 2 Mbps / ARC 5 / ARD 500 us
#define RIG_APP_CHANNEL     77
#define RIG_APP_ARC         5
#define RIG_APP_ARD_STEPS   1       // (1 + 1) * 250 us = 500 us
// Low-speed range test mode ("CTRL"): ch 80 / 250 kbps / ARC 15 / ARD 1500 us
#define RIG_CTRL_CHANNEL    80
#define RIG_CTRL_ARC        15
#define RIG_CTRL_ARD_STEPS  5       // (5 + 1) * 250 us = 1500 us

// PA level at boot: 0=MIN(-18 dBm) 1=LOW(-12) 2=HIGH(-6) 3=MAX(0).
// ICD: low power under 2 m on the bench, full power at 5-10 m on the rail.
#define RIG_DEFAULT_PA      1

// Payload profile at boot:
//   ICD    = dynamic payload length + 32-byte ACK-payload telemetry (Table 34)
//            -> Normal Crane Control Mode as written in the ICD
//   STATIC = fixed 32-byte payloads, plain auto-ACK (the 16 Oct smoke-test setup)
// Both ends MUST use the same profile; smoke_test.py sets both.
#define RIG_DEFAULT_PROFILE_ICD 1

// Lost-link threshold for the telemetry flags (Table 34 fault_flags b7 /
// system_health b0). The ICD leaves the freshness value TBD (Q10); 500 ms is a
// placeholder used for flags only - this firmware drives no actuators.
#define RIG_LINK_TIMEOUT_MS 500

// Status heartbeat on the console
#define RIG_HEARTBEAT_MS    5000

// -----------------------------------------------------------------------------
// Optional starter-kit peripherals. The radio does not need any of them.
// -----------------------------------------------------------------------------
#define RIG_ENABLE_AHT20    1       // I2C temp/humidity, logged every RIG_SENSOR_LOG_MS
#define RIG_ENABLE_ADS1220  1       // SPI ADC (pot on AIN0), shares SPI2 with the radio
#define RIG_ENABLE_SDCARD   0       // no card fitted on the bench -> only a timeout
#define RIG_ENABLE_SPWM     0       // 3-phase PWM on J7.6-J7.8; off until EEII 1 needs it
#define RIG_SENSOR_LOG_MS   5000
