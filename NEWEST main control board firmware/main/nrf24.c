// =============================================================================
// nrf24.c — minimal nRF24L01(+) driver for ESP-IDF
// =============================================================================

#include "nrf24.h"

#include <string.h>
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "nrf24";

#define NRF_CHECK(expr)                                                        \
    do {                                                                       \
        esp_err_t e__ = (expr);                                                \
        if (e__ != ESP_OK) {                                                   \
            ESP_LOGE(TAG, "%s failed: %s", #expr, esp_err_to_name(e__));       \
            return e__;                                                        \
        }                                                                      \
    } while (0)

static esp_err_t nrf24_transfer(nrf24_t *r, uint8_t *buf, size_t len)
{
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = buf,
        .rx_buffer = buf,
    };
    return spi_device_polling_transmit(r->spi, &t);
}

esp_err_t nrf24_write_reg(nrf24_t *r, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = { (uint8_t)(NRF24_CMD_W_REGISTER | (reg & 0x1F)), value };
    return nrf24_transfer(r, buf, sizeof(buf));
}

static esp_err_t nrf24_read_reg_ex(nrf24_t *r, uint8_t reg, uint8_t *value, uint8_t *status)
{
    uint8_t buf[2] = { (uint8_t)(NRF24_CMD_R_REGISTER | (reg & 0x1F)), 0xFF };
    esp_err_t err = nrf24_transfer(r, buf, sizeof(buf));
    if (err == ESP_OK) {
        if (status) { *status = buf[0]; }
        if (value)  { *value = buf[1]; }
    }
    return err;
}

esp_err_t nrf24_read_reg(nrf24_t *r, uint8_t reg, uint8_t *value)
{
    return nrf24_read_reg_ex(r, reg, value, NULL);
}

uint8_t nrf24_status(nrf24_t *r)
{
    uint8_t status = 0;
    if (nrf24_read_reg_ex(r, NRF24_REG_STATUS, NULL, &status) != ESP_OK) {
        return 0xFF;
    }
    return status;
}

// Fixed buffer size: accommodates 1 cmd byte + 32 payload bytes (prevents stack overflow)
static esp_err_t nrf24_write_bytes(nrf24_t *r, uint8_t cmd, const uint8_t *data, size_t len)
{
    uint8_t buf[1 + NRF24_PAYLOAD_SIZE];
    if (len > NRF24_PAYLOAD_SIZE) {
        return ESP_ERR_INVALID_ARG;
    }
    buf[0] = cmd;
    memcpy(&buf[1], data, len);
    return nrf24_transfer(r, buf, 1 + len);
}

static esp_err_t nrf24_read_bytes(nrf24_t *r, uint8_t cmd, uint8_t *data, size_t len)
{
    uint8_t buf[1 + NRF24_PAYLOAD_SIZE];
    buf[0] = cmd;
    memset(&buf[1], 0xFF, len);
    esp_err_t err = nrf24_transfer(r, buf, 1 + len);
    if (err == ESP_OK) {
        memcpy(data, &buf[1], len);
    }
    return err;
}

esp_err_t nrf24_init(nrf24_t *r, spi_device_handle_t spi, gpio_num_t ce_gpio)
{
    memset(r, 0, sizeof(*r));
    r->spi = spi;
    r->ce_gpio = ce_gpio;

    if (ce_gpio >= 0) {
        gpio_config_t ce_cfg = {
            .pin_bit_mask = 1ULL << ce_gpio,
            .mode = GPIO_MODE_OUTPUT,
        };
        ESP_ERROR_CHECK(gpio_config(&ce_cfg));
        gpio_set_level(ce_gpio, 0);
    }

    esp_err_t err = nrf24_write_reg(r, NRF24_REG_CONFIG,
                                    NRF24_CFG_PWR_UP | NRF24_CFG_EN_CRC | NRF24_CFG_CRCO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SPI transfer failed (%s) - check CSN/SCK/MOSI/MISO on J1", esp_err_to_name(err));
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(5));
    return ESP_OK;
}

esp_err_t nrf24_probe(nrf24_t *r)
{
    esp_err_t err = nrf24_write_reg(r, NRF24_REG_RF_CH, 77);
    uint8_t ch = 0;
    if (err == ESP_OK) { err = nrf24_read_reg(r, NRF24_REG_RF_CH, &ch); }
    if (err != ESP_OK) { return err; }
    if ((ch & 0x7F) != 77) {
        ESP_LOGE(TAG, "RF_CH read-back 0x%02X != 77 - SPI wiring/power fault", ch);
        return ESP_ERR_INVALID_RESPONSE;
    }

    uint8_t setup = 0;
    err = nrf24_read_reg(r, NRF24_REG_RF_SETUP, &setup);
    if (err != ESP_OK) { return err; }
    err = nrf24_write_reg(r, NRF24_REG_RF_SETUP, (uint8_t)(setup | (1u << 5)));
    if (err != ESP_OK) { return err; }
    uint8_t probe = 0;
    err = nrf24_read_reg(r, NRF24_REG_RF_SETUP, &probe);
    if (err != ESP_OK) { return err; }
    r->is_plus = (probe & (1u << 5)) != 0;
    err = nrf24_write_reg(r, NRF24_REG_RF_SETUP, setup);

    ESP_LOGI(TAG, "probe ok: RF_CH=77 rw, RF_SETUP=0x%02X, %s", setup,
             r->is_plus ? "nRF24L01+ (P-variant)" : "nRF24L01 (classic)");
    return err;
}

static esp_err_t nrf24_write_feature(nrf24_t *r, uint8_t value)
{
    esp_err_t err = nrf24_write_reg(r, NRF24_REG_FEATURE, value);
    uint8_t readback = 0;
    if (err == ESP_OK) { err = nrf24_read_reg(r, NRF24_REG_FEATURE, &readback); }
    if (err != ESP_OK) { return err; }
    if (readback == value) { return ESP_OK; }

    uint8_t act[2] = { NRF24_CMD_ACTIVATE, 0x73 };
    err = nrf24_transfer(r, act, sizeof(act));
    if (err != ESP_OK) { return err; }
    err = nrf24_write_reg(r, NRF24_REG_FEATURE, value);
    if (err != ESP_OK) { return err; }
    err = nrf24_read_reg(r, NRF24_REG_FEATURE, &readback);
    if (err == ESP_OK && readback != value) {
        ESP_LOGE(TAG, "FEATURE write failed (read 0x%02X, wanted 0x%02X)", readback, value);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return err;
}

esp_err_t nrf24_configure(nrf24_t *r, const nrf24_config_t *cfg)
{
    if (cfg->addr5 == NULL || cfg->arc > 15 || cfg->ard_steps > 15 || cfg->pa_dbm > 3) {
        return ESP_ERR_INVALID_ARG;
    }
    r->ack_payload = cfg->ack_payload;

    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_EN_AA, 0x03));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_EN_RXADDR, 0x01));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_SETUP_AW, 0x03));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_SETUP_RETR,
                              (uint8_t)((cfg->ard_steps << 4) | cfg->arc)));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_RF_CH, cfg->channel));

    uint8_t rf_setup = (uint8_t)(((cfg->pa_dbm & 0x03) << 1) | (cfg->rate_2mbps ? (1u << 3) : 0));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_RF_SETUP, rf_setup));

    NRF_CHECK(nrf24_write_bytes(r, (uint8_t)(NRF24_CMD_W_REGISTER | NRF24_REG_RX_ADDR_P0),
                                cfg->addr5, 5));
    NRF_CHECK(nrf24_write_bytes(r, (uint8_t)(NRF24_CMD_W_REGISTER | NRF24_REG_TX_ADDR),
                                cfg->addr5, 5));
    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_RX_PW_P0, NRF24_PAYLOAD_SIZE));

    if (cfg->ack_payload) {
        NRF_CHECK(nrf24_write_feature(r, (1u << 2) | (1u << 1)));
        NRF_CHECK(nrf24_write_reg(r, NRF24_REG_DYNPD, 0x01));
    } else {
        NRF_CHECK(nrf24_write_feature(r, 0x00));
        NRF_CHECK(nrf24_write_reg(r, NRF24_REG_DYNPD, 0x00));
    }

    NRF_CHECK(nrf24_write_reg(r, NRF24_REG_STATUS, 0x70));
    {
        uint8_t flush_tx[1] = { NRF24_CMD_FLUSH_TX };
        uint8_t flush_rx[1] = { NRF24_CMD_FLUSH_RX };
        NRF_CHECK(nrf24_transfer(r, flush_tx, 1));
        NRF_CHECK(nrf24_transfer(r, flush_rx, 1));
    }

    ESP_LOGI(TAG, "configured: ch=%u rate=%u pa=%u ARC=%u ARD=%uus addr='%c%c%c%c%c' mode=%s",
             cfg->channel, cfg->rate_2mbps ? 2000 : 1000, (unsigned)cfg->pa_dbm, (unsigned)cfg->arc,
             (unsigned)(250 + 250 * cfg->ard_steps), cfg->addr5[0], cfg->addr5[1], cfg->addr5[2],
             cfg->addr5[3], cfg->addr5[4], cfg->ack_payload ? "DPL+ACKPAY" : "static32");
    return ESP_OK;
}

esp_err_t nrf24_start_listening(nrf24_t *r)
{
    esp_err_t err = nrf24_write_reg(r, NRF24_REG_CONFIG,
                                    NRF24_CFG_PRIM_RX | NRF24_CFG_PWR_UP |
                                    NRF24_CFG_EN_CRC | NRF24_CFG_CRCO);
    if (err != ESP_OK) { return err; }
    if (r->ce_gpio >= 0) {
        gpio_set_level(r->ce_gpio, 1);
    }
    esp_rom_delay_us(150);
    ESP_LOGI(TAG, "listening (PRIM_RX, CE=%d)", (int)r->ce_gpio);
    return ESP_OK;
}

bool nrf24_payload_available(nrf24_t *r)
{
    uint8_t fifo = 0;
    if (nrf24_read_reg(r, NRF24_REG_FIFO_STATUS, &fifo) != ESP_OK) {
        return false;
    }
    return (fifo & 0x01) == 0;
}

esp_err_t nrf24_read_payload(nrf24_t *r, uint8_t out[NRF24_PAYLOAD_SIZE])
{
    esp_err_t err = nrf24_read_bytes(r, NRF24_CMD_R_RX_PAYLOAD, out, NRF24_PAYLOAD_SIZE);
    if (err != ESP_OK) { return err; }
    return nrf24_write_reg(r, NRF24_REG_STATUS, 0x70);
}

esp_err_t nrf24_write_ack_payload(nrf24_t *r, const uint8_t in[NRF24_PAYLOAD_SIZE])
{
    if (!r->ack_payload) { return ESP_ERR_NOT_SUPPORTED; }
    return nrf24_write_bytes(r, NRF24_CMD_W_ACK_PAYLOAD, in, NRF24_PAYLOAD_SIZE);
}