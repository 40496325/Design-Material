#include "nrf24.h"

#include <string.h>
#include "esp_rom_sys.h"

// ---- SPI commands (spec section 8.3.1) ----
#define CMD_R_REGISTER    0x00
#define CMD_W_REGISTER    0x20
#define CMD_R_RX_PAYLOAD  0x61
#define CMD_W_TX_PAYLOAD  0xA0
#define CMD_FLUSH_TX      0xE1
#define CMD_FLUSH_RX      0xE2
#define CMD_ACTIVATE      0x50
#define CMD_R_RX_PL_WID   0x60
#define CMD_W_ACK_PAYLOAD 0xA8
#define CMD_NOP           0xFF

// One full-duplex SPI transaction: tx[0] = command, rest = data. rx[0] = STATUS.
static void xfer(nrf24_t *dev, const uint8_t *tx, uint8_t *rx, int len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.length = (size_t)len * 8;
    t.tx_buffer = tx;
    t.rx_buffer = rx;
    // Blocking; the bus lock arbitrates with the ADS1220 / SD card on SPI2.
    spi_device_transmit(dev->spi, &t);
}

esp_err_t nrf24_attach(nrf24_t *dev, spi_host_device_t host, int csn_gpio, int clock_hz,
                       nrf24_ce_fn ce_fn, void *ce_ctx)
{
    memset(dev, 0, sizeof(*dev));
    dev->ce_fn = ce_fn;
    dev->ce_ctx = ce_ctx;
    dev->ce_level = -1;

    spi_device_interface_config_t cfg = {
        .mode = 0,                    // CPOL 0 / CPHA 0 (spec 8.3.2)
        .clock_speed_hz = clock_hz,
        .spics_io_num = csn_gpio,
        .queue_size = 2,
        .cs_ena_pretrans = 1,         // a little Tcc setup margin on jumper leads
        .cs_ena_posttrans = 1,
    };
    esp_err_t err = spi_bus_add_device(host, &cfg, &dev->spi);
    if (err != ESP_OK) {
        return err;
    }
    nrf24_ce(dev, 0);
    return ESP_OK;
}

uint8_t nrf24_command(nrf24_t *dev, uint8_t cmd)
{
    uint8_t tx[1] = {cmd}, rx[1] = {0};
    xfer(dev, tx, rx, 1);
    return rx[0];
}

uint8_t nrf24_status(nrf24_t *dev)
{
    return nrf24_command(dev, CMD_NOP);
}

uint8_t nrf24_read_reg(nrf24_t *dev, uint8_t reg)
{
    uint8_t tx[2] = {(uint8_t)(CMD_R_REGISTER | (reg & 0x1F)), CMD_NOP};
    uint8_t rx[2] = {0};
    xfer(dev, tx, rx, 2);
    return rx[1];
}

uint8_t nrf24_write_reg(nrf24_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t tx[2] = {(uint8_t)(CMD_W_REGISTER | (reg & 0x1F)), value};
    uint8_t rx[2] = {0};
    xfer(dev, tx, rx, 2);
    return rx[0];
}

void nrf24_read_regn(nrf24_t *dev, uint8_t reg, uint8_t *buf, int len)
{
    uint8_t tx[6] = {0}, rx[6] = {0};
    if (len > 5) len = 5;
    tx[0] = (uint8_t)(CMD_R_REGISTER | (reg & 0x1F));
    memset(&tx[1], CMD_NOP, (size_t)len);
    xfer(dev, tx, rx, len + 1);
    memcpy(buf, &rx[1], (size_t)len);
}

void nrf24_write_regn(nrf24_t *dev, uint8_t reg, const uint8_t *buf, int len)
{
    uint8_t tx[6] = {0}, rx[6] = {0};
    if (len > 5) len = 5;
    tx[0] = (uint8_t)(CMD_W_REGISTER | (reg & 0x1F));
    memcpy(&tx[1], buf, (size_t)len);
    xfer(dev, tx, rx, len + 1);
}

uint8_t nrf24_rx_payload_width(nrf24_t *dev)
{
    uint8_t tx[2] = {CMD_R_RX_PL_WID, CMD_NOP}, rx[2] = {0};
    xfer(dev, tx, rx, 2);
    return rx[1];
}

void nrf24_read_payload(nrf24_t *dev, uint8_t *buf, int len)
{
    uint8_t tx[33], rx[33];
    if (len > 32) len = 32;
    memset(tx, CMD_NOP, sizeof(tx));
    tx[0] = CMD_R_RX_PAYLOAD;
    xfer(dev, tx, rx, len + 1);
    memcpy(buf, &rx[1], (size_t)len);
}

void nrf24_write_ack_payload(nrf24_t *dev, uint8_t pipe, const uint8_t *buf, int len)
{
    uint8_t tx[33], rx[33];
    if (len > 32) len = 32;
    tx[0] = (uint8_t)(CMD_W_ACK_PAYLOAD | (pipe & 0x07));
    memcpy(&tx[1], buf, (size_t)len);
    xfer(dev, tx, rx, len + 1);
}

void nrf24_write_tx_payload(nrf24_t *dev, const uint8_t *buf, int len)
{
    uint8_t tx[33], rx[33];
    if (len > 32) len = 32;
    tx[0] = CMD_W_TX_PAYLOAD;
    memcpy(&tx[1], buf, (size_t)len);
    xfer(dev, tx, rx, len + 1);
}

void nrf24_flush_rx(nrf24_t *dev) { nrf24_command(dev, CMD_FLUSH_RX); }
void nrf24_flush_tx(nrf24_t *dev) { nrf24_command(dev, CMD_FLUSH_TX); }

void nrf24_clear_irqs(nrf24_t *dev)
{
    nrf24_write_reg(dev, NRF_REG_STATUS, NRF_STATUS_RX_DR | NRF_STATUS_TX_DS | NRF_STATUS_MAX_RT);
}

void nrf24_ce(nrf24_t *dev, int level)
{
    level = level ? 1 : 0;
    if (dev->ce_fn) {
        dev->ce_fn(dev->ce_ctx, level);
    }
    dev->ce_level = level;
}

esp_err_t nrf24_probe(nrf24_t *dev, uint8_t *setup_aw_out)
{
    uint8_t aw = nrf24_read_reg(dev, NRF_REG_SETUP_AW);
    if (setup_aw_out) *setup_aw_out = aw;
    if (aw == 0x00 || aw > 0x03) {
        return ESP_ERR_NOT_FOUND;            // 0x00 = MISO stuck low / no power, 0xFF = floating
    }
    const uint8_t old_ch = nrf24_read_reg(dev, NRF_REG_RF_CH);
    const uint8_t patterns[2] = {0x55, 0x2A};
    for (int i = 0; i < 2; i++) {
        nrf24_write_reg(dev, NRF_REG_RF_CH, patterns[i]);
        if (nrf24_read_reg(dev, NRF_REG_RF_CH) != patterns[i]) {
            nrf24_write_reg(dev, NRF_REG_RF_CH, old_ch);
            return ESP_ERR_INVALID_RESPONSE; // MOSI/SCK problem: reads work, writes don't stick
        }
    }
    nrf24_write_reg(dev, NRF_REG_RF_CH, old_ch);
    return ESP_OK;
}

void nrf24_detect_variant(nrf24_t *dev)
{
    // + variant: RF_DR_LOW (250 kbps) is writable. Same check the RF24 library uses.
    const uint8_t rf = nrf24_read_reg(dev, NRF_REG_RF_SETUP);
    nrf24_write_reg(dev, NRF_REG_RF_SETUP, (uint8_t)((rf & ~NRF_RF_DR_HIGH) | NRF_RF_DR_LOW));
    dev->is_plus = (nrf24_read_reg(dev, NRF_REG_RF_SETUP) & NRF_RF_DR_LOW) != 0;
    nrf24_write_reg(dev, NRF_REG_RF_SETUP, rf);

    // Non-+ parts need ACTIVATE 0x73 before FEATURE / DYNPD are writable.
    nrf24_write_reg(dev, NRF_REG_FEATURE, NRF_FEATURE_EN_DPL);
    if (nrf24_read_reg(dev, NRF_REG_FEATURE) != NRF_FEATURE_EN_DPL) {
        uint8_t tx[2] = {CMD_ACTIVATE, 0x73}, rx[2];
        xfer(dev, tx, rx, 2);
    }
    nrf24_write_reg(dev, NRF_REG_FEATURE, 0x00);
}

const char *nrf24_pa_name(int pa)
{
    static const char *names[4] = {"MIN", "LOW", "HIGH", "MAX"};
    return names[pa & 3];
}

int nrf24_pa_dbm(int pa)
{
    static const int dbm[4] = {-18, -12, -6, 0};
    return dbm[pa & 3];
}
