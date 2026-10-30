#include "sdcard.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sdcard";

#define PROBE_RETRY_COUNT 5
#define PROBE_RETRY_DELAY_MS 20

esp_err_t sdcard_init(spi_host_device_t host_id, gpio_num_t cs_gpio, sdcard_handle_t *out_handle) {
    out_handle->host = (sdmmc_host_t)SDSPI_HOST_DEFAULT();
    out_handle->host.slot = host_id;

    esp_err_t err = out_handle->host.init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "host init failed (%s)", esp_err_to_name(err));
        return err;
    }

    sdspi_device_config_t dev_config = SDSPI_DEVICE_CONFIG_DEFAULT();
    dev_config.gpio_cs = cs_gpio;
    dev_config.host_id = host_id;

    // On success this reassigns host.slot from the SPI host id to a device
    // handle - required by sdmmc_card_init() below.
    return sdspi_host_init_device(&dev_config, &out_handle->host.slot);
}

esp_err_t sdcard_probe(sdcard_handle_t *handle) {
    esp_err_t err = ESP_FAIL;

    // The first identification attempt sometimes loses a race with the
    // card's own internal power-on reset timing; a same-boot retry after a
    // short delay clears it up on some cards without any wiring changes.
    for (int attempt = 1; attempt <= PROBE_RETRY_COUNT; attempt++) {
        err = sdmmc_card_init(&handle->host, &handle->card);
        if (err == ESP_OK) {
            if (attempt > 1) {
                ESP_LOGW(TAG, "card init succeeded on attempt %d", attempt);
            }
            break;
        }
        ESP_LOGW(TAG, "card init attempt %d/%d failed (%s)", attempt, PROBE_RETRY_COUNT, esp_err_to_name(err));
        vTaskDelay(pdMS_TO_TICKS(PROBE_RETRY_DELAY_MS));
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "card init failed after %d attempts (%s)", PROBE_RETRY_COUNT, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "card: %s, %lluMB", handle->card.cid.name,
             ((uint64_t)handle->card.csd.capacity) * handle->card.csd.sector_size / (1024 * 1024));
    return ESP_OK;
}
