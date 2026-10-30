// =============================================================================
// nrf24.h - minimal nRF24L01(+) register driver for ESP-IDF (spi_master)
// =============================================================================
// Written for this project from the nRF24L01+ product specification v1.0
// (Increment 2 datasheets/NRF24L01.PDF). No Arduino / RF24 dependency.
//
// The driver only moves bytes: register read/write, payload read, ACK-payload
// write, FIFO flush, CE. The link policy (channel, profile, what to do with a
// frame) lives in rf_link.c.
// =============================================================================
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Registers (spec section 9) ----
#define NRF_REG_CONFIG      0x00
#define NRF_REG_EN_AA       0x01
#define NRF_REG_EN_RXADDR   0x02
#define NRF_REG_SETUP_AW    0x03
#define NRF_REG_SETUP_RETR  0x04
#define NRF_REG_RF_CH       0x05
#define NRF_REG_RF_SETUP    0x06
#define NRF_REG_STATUS      0x07
#define NRF_REG_OBSERVE_TX  0x08
#define NRF_REG_RPD         0x09
#define NRF_REG_RX_ADDR_P0  0x0A
#define NRF_REG_RX_ADDR_P1  0x0B
#define NRF_REG_TX_ADDR     0x10
#define NRF_REG_RX_PW_P0    0x11
#define NRF_REG_RX_PW_P1    0x12
#define NRF_REG_FIFO_STATUS 0x17
#define NRF_REG_DYNPD       0x1C
#define NRF_REG_FEATURE     0x1D

// CONFIG bits
#define NRF_CONFIG_PRIM_RX  0x01
#define NRF_CONFIG_PWR_UP   0x02
#define NRF_CONFIG_CRCO     0x04   // 1 = 2-byte CRC
#define NRF_CONFIG_EN_CRC   0x08

// STATUS bits
#define NRF_STATUS_RX_DR    0x40
#define NRF_STATUS_TX_DS    0x20
#define NRF_STATUS_MAX_RT   0x10
#define NRF_STATUS_RX_P_NO(s) (((s) >> 1) & 0x07)   // 7 = RX FIFO empty

// FIFO_STATUS bits
#define NRF_FIFO_RX_EMPTY   0x01
#define NRF_FIFO_RX_FULL    0x02
#define NRF_FIFO_TX_EMPTY   0x10
#define NRF_FIFO_TX_FULL    0x20

// RF_SETUP bits
#define NRF_RF_CONT_WAVE    0x80
#define NRF_RF_DR_LOW       0x20
#define NRF_RF_PLL_LOCK     0x10
#define NRF_RF_DR_HIGH      0x08
#define NRF_RF_PWR(pa)      (((pa) & 0x03) << 1)

// FEATURE bits
#define NRF_FEATURE_EN_DPL     0x04
#define NRF_FEATURE_EN_ACK_PAY 0x02
#define NRF_FEATURE_EN_DYN_ACK 0x01

typedef void (*nrf24_ce_fn)(void *ctx, int level);

typedef struct {
    spi_device_handle_t spi;
    nrf24_ce_fn ce_fn;
    void *ce_ctx;
    int ce_level;
    bool is_plus;      // nRF24L01+ (250 kbps + RPD available)
} nrf24_t;

/** Adds the radio to an already-initialised SPI bus. Does not touch radio registers. */
esp_err_t nrf24_attach(nrf24_t *dev, spi_host_device_t host, int csn_gpio, int clock_hz,
                       nrf24_ce_fn ce_fn, void *ce_ctx);

/** Single-byte command (FLUSH_TX, NOP, ...). Returns STATUS. */
uint8_t nrf24_command(nrf24_t *dev, uint8_t cmd);
uint8_t nrf24_status(nrf24_t *dev);

uint8_t nrf24_read_reg(nrf24_t *dev, uint8_t reg);
uint8_t nrf24_write_reg(nrf24_t *dev, uint8_t reg, uint8_t value);   // returns STATUS
void nrf24_read_regn(nrf24_t *dev, uint8_t reg, uint8_t *buf, int len);
void nrf24_write_regn(nrf24_t *dev, uint8_t reg, const uint8_t *buf, int len);

/** R_RX_PL_WID - width of the payload at the top of the RX FIFO (dynamic payloads). */
uint8_t nrf24_rx_payload_width(nrf24_t *dev);
/** R_RX_PAYLOAD - pops one payload of `len` bytes from the RX FIFO. */
void nrf24_read_payload(nrf24_t *dev, uint8_t *buf, int len);
/** W_ACK_PAYLOAD - queue a payload to go back inside the next ACK on `pipe`. */
void nrf24_write_ack_payload(nrf24_t *dev, uint8_t pipe, const uint8_t *buf, int len);
/** W_TX_PAYLOAD - queue a payload for transmission (PTX mode). */
void nrf24_write_tx_payload(nrf24_t *dev, const uint8_t *buf, int len);

void nrf24_flush_rx(nrf24_t *dev);
void nrf24_flush_tx(nrf24_t *dev);
void nrf24_clear_irqs(nrf24_t *dev);

void nrf24_ce(nrf24_t *dev, int level);

/**
 * SPI sanity check before any configuration: SETUP_AW must read back a legal
 * value and a scratch write to RF_CH must read back exactly. Catches dead MISO
 * (0x00), floating MISO (0xFF), no power, wrong CSN.
 */
esp_err_t nrf24_probe(nrf24_t *dev, uint8_t *setup_aw_out);

/** Detects the + variant (RF_DR_LOW sticks) and, for a non-+ part, sends ACTIVATE. */
void nrf24_detect_variant(nrf24_t *dev);

/** Name for a PA index 0..3. */
const char *nrf24_pa_name(int pa);
int nrf24_pa_dbm(int pa);

#ifdef __cplusplus
}
#endif
