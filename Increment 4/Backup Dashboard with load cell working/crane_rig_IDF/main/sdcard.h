// Talks to a micro-SD card in SPI mode (as opposed to the faster but
// more-pins native SDIO/SD-bus mode) over the same shared SPI bus as the
// ADS1220, using ESP-IDF's own sdmmc/sdspi components rather than anything
// hand-rolled here. This module only gets as far as "identify the card and
// read its name/capacity" (sdcard_probe()) - mounting a FAT filesystem on
// it (so you could actually open/read/write files) is a further step not
// implemented yet, using ESP-IDF's esp_vfs_fat_sdspi_mount().

#pragma once

#include "driver/sdspi_host.h"
#include "driver/spi_master.h"
#include "sdmmc_cmd.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    sdmmc_host_t host;
    sdmmc_card_t card;
} sdcard_handle_t;

/**
 * @brief Attach an SD card (SPI mode) to an already-initialized SPI bus.
 *
 * @param host_id     SPI host the bus was set up on (e.g. SPI2_HOST)
 * @param cs_gpio     Chip-select GPIO (MMC_CS)
 * @param out_handle  Filled in on success
 */
esp_err_t sdcard_init(spi_host_device_t host_id, gpio_num_t cs_gpio, sdcard_handle_t *out_handle);

/**
 * @brief Run the SD card identification sequence (CMD0/CMD8/ACMD41/CMD2/CMD3/CMD9)
 *        and populate handle->card with the card's reported name/capacity/etc.
 *        Does not touch any filesystem - no mount, no read/write of file data.
 * @return ESP_OK if a card responded and identified itself, an SD/MMC error otherwise.
 */
esp_err_t sdcard_probe(sdcard_handle_t *handle);

#ifdef __cplusplus
}
#endif
