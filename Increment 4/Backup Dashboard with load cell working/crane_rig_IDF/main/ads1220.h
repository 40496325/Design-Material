// Driver for the ADS1220, a 24-bit precision SPI analog-to-digital
// converter (ADC) - see Datasheets/ADS1220.pdf. On this board it's wired to
// read the onboard potentiometer's wiper voltage on channel AIN0, but it
// has 4 single-ended input channels (AIN0-AIN3) total.
//
// "24-bit" means each conversion returns a value with far finer resolution
// than the ESP32-S3's own built-in ADC (12-bit) - useful whenever you need
// to resolve small voltage differences precisely, at the cost of needing a
// dedicated chip and a few microseconds-to-milliseconds per conversion
// (see DRDY_TIMEOUT_US in ads1220.c) rather than reading a GPIO pin directly.

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/spi_master.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    spi_device_handle_t spi;
    int drdy_gpio;
} ads1220_handle_t;

/** @brief Attaches the ADS1220 as a SPI device on the given bus (SPI mode 1, per datasheet). */
esp_err_t ads1220_init(spi_host_device_t host, int cs_gpio, int drdy_gpio, ads1220_handle_t *out_handle);

/**
 * @brief Resets the chip and confirms all four config registers read back
 *        their documented post-reset default (0x00 each) - a real
 *        functional check, not just "the SPI transaction didn't error."
 */
esp_err_t ads1220_probe(ads1220_handle_t *handle);

/**
 * @brief Configures channel AIN0-AIN3 as a ratiometric single-ended input
 *        against AVSS (gain=1, PGA bypassed - required whenever AINN=AVSS
 *        per the datasheet; VREF=AVDD-AVSS, so the result is a ratio of the
 *        input to the supply rail, correct for a resistive divider such as
 *        a potentiometer excited from AVDD), triggers one single-shot
 *        conversion, waits for DRDY, and returns the raw 24-bit
 *        two's-complement result.
 */
esp_err_t ads1220_read_single_ended(ads1220_handle_t *handle, uint8_t channel, int32_t *out_code);

/**
 * REII (Increment 4) - load-cell bridge mode, ICD IF13.0.
 *
 * Configures the ADS1220 for a 4-wire strain-gauge bridge on J6:
 *   MUX AIN0 (J6.5 SIG+) vs AIN1 (J6.6 SIG-), PGA enabled, gain = 1 << gain_log2
 *   20 SPS, continuous conversion, VREF = AVDD-AVSS (the bridge is excited from the
 *   same +3.3VP rail on J6.1/J6.2, so the result is ratiometric), 50 Hz FIR rejection.
 * Reads the three config registers back; ESP_ERR_INVALID_RESPONSE on mismatch.
 * Starts the first conversion.
 */
esp_err_t ads1220_configure_bridge(ads1220_handle_t *handle, uint8_t gain_log2);

/** Waits up to timeout_ms for DRDY and returns the latest signed 24-bit code. */
esp_err_t ads1220_read_continuous(ads1220_handle_t *handle, int timeout_ms, int32_t *out_code);

#ifdef __cplusplus
}
#endif
