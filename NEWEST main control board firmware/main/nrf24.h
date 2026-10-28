// =============================================================================
// nrf24.h — minimal nRF24L01(+) register-level driver (ESP-IDF, no Arduino)
// =============================================================================
//
// Just enough Enhanced-ShockBurst functionality to run the REII crane link on
// the ESP32-S3 core board (REII SoW item 1: "completing the stubbed nRF24 SPI
// driver on the ESP32 controller side").
//
// Wiring (docs/27_reii_nrf_wiring_and_config.md, PROPOSED set now implemented):
//   J1 "Comms Exander": CSN = pin1 = GPIO17 (SPI_xCS), MOSI = pin2 = GPIO11,
//   SCK = pin3 = GPIO12, MISO = pin4 = GPIO13 (all shared with the onboard
//   SPI2 bus), VCC = pin8 = +3.3VP (GPIO47-gated), GND = pin5/6.
//   CE = J7.10 (net IO_B) = GPIO10. IRQ not used — STATUS is polled.
//
// Compatibility rule: this driver must program the radio EXACTLY like the
// NodeMCU-32S groundstation scaffold (project_sources/03_increment_3/
// groundstation_esp/, Arduino RF24 v1.4.9): 32-byte STATIC payloads (DPL off),
// 5-byte addresses, CRC-16, auto-ACK on pipes 0-1, ch 77 / 2 Mbps / PA_MAX /
// ARC 5 / ARD 500 us. Dynamic payloads + ACK payload are supported behind
// NRF24_CONFIG_ACK_PAYLOAD but must be enabled on BOTH ends together.
//
// Address byte order (classic pitfall): nRF24 address registers are sent
// LSB-first per byte, first array byte transmitted first on the air. Writing
// the 5 bytes {'R','E','I','I','2'} produces air order "REII2" — the same
// on-wire bytes the RF24 library produces for ADDR_PEER (evidence: measured
// TX-address read-back 0x3249494552 = "REII2", results/old/evidence/...).
// =============================================================================

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NRF24_PAYLOAD_SIZE 32

// nRF24 register map (subset) — nRF24L01+ datasheet Table 9.
#define NRF24_REG_CONFIG      0x00
#define NRF24_REG_EN_AA       0x01
#define NRF24_REG_EN_RXADDR   0x02
#define NRF24_REG_SETUP_AW    0x03
#define NRF24_REG_SETUP_RETR  0x04
#define NRF24_REG_RF_CH       0x05
#define NRF24_REG_RF_SETUP    0x06
#define NRF24_REG_STATUS      0x07
#define NRF24_REG_RX_ADDR_P0  0x0A
#define NRF24_REG_TX_ADDR     0x10
#define NRF24_REG_RX_PW_P0    0x11
#define NRF24_REG_FIFO_STATUS 0x17
#define NRF24_REG_DYNPD       0x1C
#define NRF24_REG_FEATURE     0x1D

// SPI commands — Table 20.
#define NRF24_CMD_R_REGISTER    0x00
#define NRF24_CMD_W_REGISTER    0x20
#define NRF24_CMD_R_RX_PAYLOAD  0x61
#define NRF24_CMD_W_TX_PAYLOAD  0xA0
#define NRF24_CMD_FLUSH_TX      0xE1
#define NRF24_CMD_FLUSH_RX      0xE2
#define NRF24_CMD_W_ACK_PAYLOAD 0xA8  // | pipe number
#define NRF24_CMD_ACTIVATE      0x50  // followed by 0x73 (legacy FEATURE access)

// CONFIG bits
#define NRF24_CFG_PRIM_RX (1u << 0)
#define NRF24_CFG_PWR_UP  (1u << 1)
#define NRF24_CFG_CRCO    (1u << 2)  // 1 = 2-byte CRC
#define NRF24_CFG_EN_CRC  (1u << 3)

// STATUS bits
#define NRF24_ST_RX_DR (1u << 6)
#define NRF24_ST_TX_DS (1u << 5)
#define NRF24_ST_MAX_RT (1u << 4)

typedef struct {
    spi_device_handle_t spi;   // bus device with CSN on GPIO17 (hardware CS)
    gpio_num_t ce_gpio;        // CE on GPIO10 (J7.10)
    bool is_plus;              // nRF24L01+ (250 kbps capable / P-variant)
    bool ack_payload;          // DPL + ACK-payload mode (must match the peer!)
} nrf24_t;

typedef struct {
    uint8_t channel;       // RF_CH 0..125 (77 = 2477 MHz, D-00a)
    bool rate_2mbps;       // true = 2 Mbps (P-variant), false = 1 Mbps
    uint8_t pa_dbm;        // RF_PWR field 0..3 (3 = PA_MAX / 0 dBm)
    uint8_t arc;           // auto-retransmit count 0..15 (5)
    uint8_t ard_steps;     // ARD field 0..15 -> (250 + 250*steps) us (1 = 500 us)
    bool ack_payload;      // enable FEATURE EN_DPL|EN_ACK_PAY + DYNPD pipe0
    const uint8_t *addr5;  // 5-byte address: RX pipe0 = TX addr (for auto-ACK)
} nrf24_config_t;

// Bring the chip out of reset: CE low, power up, basic sanity. SPI device must
// already be added to the (shared) SPI2 bus with CSN = GPIO17.
esp_err_t nrf24_init(nrf24_t *r, spi_device_handle_t spi, gpio_num_t ce_gpio);

// Probe: RF_CH write/read-back (wiring check) + P-variant detection.
esp_err_t nrf24_probe(nrf24_t *r);

// Program the link configuration (registers written one at a time, read back
// where cheap). Call before nrf24_start_listening().
esp_err_t nrf24_configure(nrf24_t *r, const nrf24_config_t *cfg);

// PRIM_RX | PWR_UP + CE high -> receiving (130 us settle handled).
esp_err_t nrf24_start_listening(nrf24_t *r);

// True when at least one payload sits in the RX FIFO (STATUS.RX_DR or
// FIFO_STATUS.RX_EMPTY == 0).
bool nrf24_payload_available(nrf24_t *r);

// Pop one 32-byte payload; clears STATUS.RX_DR.
esp_err_t nrf24_read_payload(nrf24_t *r, uint8_t out[NRF24_PAYLOAD_SIZE]);

// Optional (ACK-payload mode): load pipe0 ACK payload to be piggy-backed onto
// the next auto-ACK. Requires cfg.ack_payload == true on BOTH ends.
esp_err_t nrf24_write_ack_payload(nrf24_t *r, const uint8_t in[NRF24_PAYLOAD_SIZE]);

// Raw register access (used by the init/bring-up logging).
esp_err_t nrf24_write_reg(nrf24_t *r, uint8_t reg, uint8_t value);
esp_err_t nrf24_read_reg(nrf24_t *r, uint8_t reg, uint8_t *value);
uint8_t nrf24_status(nrf24_t *r);

#ifdef __cplusplus
}
#endif
