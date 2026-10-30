// Everything in this file talks to the ADS1220 using single-byte "command"
// values shifted out over SPI (send_command()), or multi-byte read/write
// register transactions (read_registers()/write_register()) - see the
// datasheet's "Commands" table (section 8.5.1.1) for where each 0x.. value
// below comes from. SPI itself has no concept of "registers" or
// "commands" - that structure is entirely this chip's own protocol, built
// on top of plain byte transfers.

#include "ads1220.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdbool.h>
#include <string.h>

static const char *TAG = "ads1220";

#define ADS1220_CMD_RESET 0x06
#define ADS1220_CMD_START 0x08
#define ADS1220_CMD_RDATA 0x10
#define ADS1220_CMD_RREG  0x20  // 001r rnnn - r r = start register, nnn = (count - 1)
#define ADS1220_CMD_WREG  0x40  // 010r rnnn - r r = start register, nnn = (count - 1)

#define PROBE_RETRY_COUNT     5
#define PROBE_RETRY_DELAY_MS 20

#define CONFIG0_REG 0x00
#define CONFIG2_REG 0x02

#define DRDY_TIMEOUT_US (200 * 1000)  // single-shot 20SPS conversion is ~50ms (datasheet Table 8-4)

static esp_err_t send_command(ads1220_handle_t *handle, uint8_t cmd)
{
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_transmit(handle->spi, &t);
}

static esp_err_t read_registers(ads1220_handle_t *handle, uint8_t start_reg, uint8_t *out_data, size_t count)
{
    uint8_t cmd = ADS1220_CMD_RREG | (start_reg << 2) | (uint8_t)(count - 1);
    uint8_t tx[5] = {cmd};
    uint8_t rx[5] = {0};

    spi_transaction_t t = {
        .length = (1 + count) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    // rx[0] is whatever the ADC happened to drive while it was still
    // clocking in the command byte, not register data - the readback starts
    // at rx[1].
    memcpy(out_data, &rx[1], count);
    return ESP_OK;
}

static esp_err_t write_register(ads1220_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t cmd = ADS1220_CMD_WREG | (reg << 2);  // nn=0 -> write 1 register
    uint8_t tx[2] = {cmd, value};
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
    };
    return spi_device_transmit(handle->spi, &t);
}

esp_err_t ads1220_init(spi_host_device_t host, int cs_gpio, int drdy_gpio, ads1220_handle_t *out_handle)
{
    spi_device_interface_config_t dev_cfg = {
        .mode = 1,
        .clock_speed_hz = 1 * 1000 * 1000,
        .spics_io_num = cs_gpio,
        .queue_size = 1,
        // DOUT doubles as DRDY and only switches into DOUT mode once CS
        // asserts - give it a few clock cycles of setup margin before the
        // first SCLK edge so we don't sample it mid-transition.
        .cs_ena_pretrans = 2,
    };
    esp_err_t err = spi_bus_add_device(host, &dev_cfg, &out_handle->spi);
    if (err != ESP_OK) {
        return err;
    }

    out_handle->drdy_gpio = drdy_gpio;
    gpio_config_t drdy_cfg = {
        .pin_bit_mask = 1ULL << drdy_gpio,
        .mode = GPIO_MODE_INPUT,
    };
    return gpio_config(&drdy_cfg);
}

esp_err_t ads1220_probe(ads1220_handle_t *handle)
{
    esp_err_t err = ESP_FAIL;

    // Same peripheral-rail power-on race seen on every other device on this
    // board - unlike an I2C NACK, an unpowered SPI slave doesn't flag the
    // problem, it just drives MISO with whatever floats, showing up here as
    // "readable but not the documented reset default". A same-boot retry
    // clears it up without any wiring changes.
    for (int attempt = 1; attempt <= PROBE_RETRY_COUNT; attempt++) {
        uint8_t pre_regs[4];
        if (read_registers(handle, 0, pre_regs, sizeof(pre_regs)) == ESP_OK) {
            ESP_LOGI(TAG, "attempt %d: before reset: %02X %02X %02X %02X",
                     attempt, pre_regs[0], pre_regs[1], pre_regs[2], pre_regs[3]);
        }

        err = send_command(handle, ADS1220_CMD_RESET);
        if (err == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(5));  // datasheet: allow the reset to complete before the next command

            uint8_t regs[4];
            err = read_registers(handle, 0, regs, sizeof(regs));
            if (err == ESP_OK) {
                bool all_zero = true;
                for (size_t i = 0; i < sizeof(regs); i++) {
                    if (regs[i] != 0x00) {
                        all_zero = false;
                        break;
                    }
                }
                if (all_zero) {
                    return ESP_OK;
                }
                ESP_LOGW(TAG, "attempt %d: unexpected register values: %02X %02X %02X %02X",
                         attempt, regs[0], regs[1], regs[2], regs[3]);
                err = ESP_ERR_INVALID_RESPONSE;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(PROBE_RETRY_DELAY_MS));
    }

    return err;
}

esp_err_t ads1220_read_single_ended(ads1220_handle_t *handle, uint8_t channel, int32_t *out_code)
{
    if (channel > 3) {
        return ESP_ERR_INVALID_ARG;
    }

    // MUX 1000b-1011b = AIN0-AIN3 vs AVSS (single-ended). Whenever AINN is
    // AVSS the datasheet requires PGA_BYPASS=1, and only gain 1/2/4 is
    // valid without the PGA - use gain=1.
    uint8_t mux = 0x08 | channel;
    uint8_t config0 = (uint8_t)(mux << 4) | 0x01;  // GAIN=000, PGA_BYPASS=1
    esp_err_t err = write_register(handle, CONFIG0_REG, config0);
    if (err != ESP_OK) {
        return err;
    }

    // VREF[1:0]=11: use AVDD-AVSS as the reference instead of the internal
    // 2.048V. Makes this ratiometric - correct for a resistive divider (e.g.
    // a potentiometer) excited from the same AVDD rail, since the reading is
    // then a pure ratio of wiper voltage to AVDD and cancels out AVDD's own
    // drift/noise, rather than being compared against an unrelated 2.048V.
    err = write_register(handle, CONFIG2_REG, 0xC0);
    if (err != ESP_OK) {
        return err;
    }

    err = send_command(handle, ADS1220_CMD_START);
    if (err != ESP_OK) {
        return err;
    }

    int64_t deadline = esp_timer_get_time() + DRDY_TIMEOUT_US;
    while (gpio_get_level(handle->drdy_gpio) != 0) {
        if (esp_timer_get_time() > deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    uint8_t tx[4] = {ADS1220_CMD_RDATA};
    uint8_t rx[4] = {0};
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }

    // 24-bit two's-complement result, MSB first, starting at rx[1] (same
    // command-byte-then-data alignment as read_registers()).
    uint32_t raw = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    if (raw & 0x800000) {
        raw |= 0xFF000000;  // sign-extend to 32 bits
    }
    *out_code = (int32_t)raw;

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// REII (Increment 4): load-cell bridge mode (ICD IF13.0, J6.5/J6.6 -> AIN0/AIN1)
// ---------------------------------------------------------------------------
#define CONFIG1_REG 0x01

esp_err_t ads1220_configure_bridge(ads1220_handle_t *handle, uint8_t gain_log2)
{
    // Config 0: MUX[7:4]=0000 (AINP=AIN0, AINN=AIN1), GAIN[3:1], PGA_BYPASS[0]=0 (PGA on,
    // required for gains above 4 and gives the high input impedance a bridge wants).
    const uint8_t cfg0 = (uint8_t)(((gain_log2 & 0x07) << 1));
    // Config 1: DR[7:5]=000 (20 SPS), MODE[4:3]=00 (normal), CM[2]=1 (continuous), TS=0, BCS=0.
    const uint8_t cfg1 = 0x04;
    // Config 2: VREF[7:6]=11 (AVDD-AVSS, ratiometric to the bridge excitation),
    // 50/60[5:4]=10 (50 Hz rejection, valid at 20 SPS), PSW=0, IDAC=off.
    const uint8_t cfg2 = 0xE0;
    esp_err_t err = write_register(handle, CONFIG0_REG, cfg0);
    if (err == ESP_OK) err = write_register(handle, CONFIG1_REG, cfg1);
    if (err == ESP_OK) err = write_register(handle, CONFIG2_REG, cfg2);
    if (err != ESP_OK) {
        return err;
    }
    uint8_t regs[3] = {0};
    err = read_registers(handle, 0, regs, sizeof(regs));
    if (err != ESP_OK) {
        return err;
    }
    if (regs[0] != cfg0 || regs[1] != cfg1 || regs[2] != cfg2) {
        ESP_LOGE(TAG, "bridge config read-back %02X %02X %02X, expected %02X %02X %02X",
                 regs[0], regs[1], regs[2], cfg0, cfg1, cfg2);
        return ESP_ERR_INVALID_RESPONSE;
    }
    return send_command(handle, ADS1220_CMD_START);   // continuous mode: one START runs forever
}

esp_err_t ads1220_read_continuous(ads1220_handle_t *handle, int timeout_ms, int32_t *out_code)
{
    int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(handle->drdy_gpio) != 0) {
        if (esp_timer_get_time() > deadline) {
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    uint8_t tx[4] = {ADS1220_CMD_RDATA};
    uint8_t rx[4] = {0};
    spi_transaction_t t = {
        .length = sizeof(tx) * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    esp_err_t err = spi_device_transmit(handle->spi, &t);
    if (err != ESP_OK) {
        return err;
    }
    uint32_t raw = ((uint32_t)rx[1] << 16) | ((uint32_t)rx[2] << 8) | rx[3];
    if (raw & 0x800000) {
        raw |= 0xFF000000;
    }
    *out_code = (int32_t)raw;
    return ESP_OK;
}
