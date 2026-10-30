// =============================================================================
// nrf24.h — minimal nRF24L01(+) register-level driver (ESP-IDF)
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

// nRF24 register map
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

// SPI commands
#define NRF24_CMD_R_REGISTER    0x00
#define NRF24_CMD_W_REGISTER    0x20
#define NRF24_CMD_R_RX_PAYLOAD  0x61
#define NRF24_CMD_W_TX_PAYLOAD  0xA0
#define NRF24_CMD_FLUSH_TX      0xE1
#define NRF24_CMD_FLUSH_RX      0xE2
#define NRF24_CMD_W_ACK_PAYLOAD 0xA8
#define NRF24_CMD_ACTIVATE      0x50

// CONFIG bits
#define NRF24_CFG_PRIM_RX (1u << 0)
#define NRF24_CFG_PWR_UP  (1u << 1)
#define NRF24_CFG_CRCO    (1u << 2)
#define NRF24_CFG_EN_CRC  (1u << 3)

// STATUS bits
#define NRF24_ST_RX_DR    (1u << 6)
#define NRF24_ST_TX_DS    (1u << 5)
#define NRF24_ST_MAX_RT   (1u << 4)

typedef struct {
    spi_device_handle_t spi;
    gpio_num_t ce_gpio;        // Set to GPIO_NUM_NC (-1) when CE is driven via MCP23017
    bool is_plus;
    bool ack_payload;
} nrf24_t;

typedef struct {
    uint8_t channel;
    bool rate_2mbps;
    uint8_t pa_dbm;
    uint8_t arc;
    uint8_t ard_steps;
    bool ack_payload;
    const uint8_t *addr5;
} nrf24_config_t;

esp_err_t nrf24_init(nrf24_t *r, spi_device_handle_t spi, gpio_num_t ce_gpio);
esp_err_t nrf24_probe(nrf24_t *r);
esp_err_t nrf24_configure(nrf24_t *r, const nrf24_config_t *cfg);
esp_err_t nrf24_start_listening(nrf24_t *r);
bool nrf24_payload_available(nrf24_t *r);
esp_err_t nrf24_read_payload(nrf24_t *r, uint8_t out[NRF24_PAYLOAD_SIZE]);
esp_err_t nrf24_write_ack_payload(nrf24_t *r, const uint8_t in[NRF24_PAYLOAD_SIZE]);
esp_err_t nrf24_write_reg(nrf24_t *r, uint8_t reg, uint8_t value);
esp_err_t nrf24_read_reg(nrf24_t *r, uint8_t reg, uint8_t *value);
uint8_t nrf24_status(nrf24_t *r);

#ifdef __cplusplus
}
#endif