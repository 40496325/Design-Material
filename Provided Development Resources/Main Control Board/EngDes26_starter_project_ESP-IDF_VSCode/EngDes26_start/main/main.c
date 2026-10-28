// =============================================================================
// EngDes26 starter project - main.c
// =============================================================================
//
// This is the entry point and "wiring diagram in code" for the board. Each
// peripheral driver lives in its own module (one .c/.h pair per chip), and
// this file is responsible for:
//   1. Bringing every peripheral up, in the right order, at boot.
//   2. Starting one FreeRTOS task per peripheral that needs to be polled
//      continuously (sensors), while one-shot setups (SPWM, status LED)
//      just get configured once and then run themselves in the background.
//
// Module map (see each header for what it does):
//   status_led.c   - WS2812 RGB LED, "breathing" idle animation
//   mcp23017.c     - I2C 16-bit GPIO expander
//   ads1220.c      - SPI 24-bit ADC (reads the onboard potentiometer)
//   sdcard.c       - SPI SD card (identification only, no filesystem yet)
//   spwm.c         - 3-phase sinusoidal PWM generator (MCPWM peripheral)
//   oled.c         - SSD1306/SH1106 I2C OLED dashboard (DISABLED by default,
//                    see "OLED support" below)
//
// WHAT'S MISSING COMPARED TO THE FULL LAB PROJECT, AND WHY
// -----------------------------------------------------------------------
// The full project also has a WiFi station + web dashboard (wifi_app.c,
// web_server.c, an embedded index.html). That has been removed completely
// from this starter, on purpose:
//
//   Turning on the ESP32-S3's radio (esp_wifi_start(), which the removed
//   wifi_app_init_sta() called) drives real RF power out of the antenna
//   pin. If no antenna is actually connected, that power has nowhere to
//   go and reflects back into the radio's power amplifier - repeatedly
//   doing this can degrade or outright kill the PA. Not every student's
//   board has an antenna connected, and there's no way for firmware to
//   detect "no antenna" before transmitting, so the only reliable fix is:
//   this starter never calls esp_wifi_start() at all. If your board *does*
//   have an antenna and you want the WiFi dashboard back, ask for the full
//   reference project rather than re-adding it here from scratch.
//
// Everything that isn't RF (I2C, SPI, PWM, the SD card, the status LED) is
// completely unaffected by this and works exactly like the full project.
// Sensor readings that used to also feed the web dashboard now just go to
// the serial log via ESP_LOGI() - open the serial monitor to see them.
// =============================================================================

#include <stdio.h>
#include <inttypes.h>
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "status_led.h"
#include "mcp23017.h"
#include "ads1220.h"
#include "sdcard.h"
#include "spwm.h"
#include "oled.h"

// -----------------------------------------------------------------------------
// OLED support - OFF by default
// -----------------------------------------------------------------------------
// Most students won't have an SSD1306/SH1106 I2C OLED wired up, so the OLED
// dashboard is compiled in (oled.c is still part of the build, so you can
// read it as a reference) but not started unless you flip this to 1.
//
// Everything below guarded by "#if OLED_ENABLED ... #endif" is the OLED
// feature: the shared display-state variables, the task that redraws the
// screen, and the init call in app_main(). The preprocessor strips all of it
// out of the build when OLED_ENABLED is 0, so there's no runtime cost and
// nothing to accidentally call if you don't have the display - the code is
// simply not there, same as if you'd commented it out by hand, just easier
// to turn back on.
//
// If you wire up an OLED (I2C, address 0x3C, SDA/SCL same bus as everything
// else below): change the 0 to a 1 and rebuild.
#define OLED_ENABLED 0

// TODO: confirm against this board's schematic - carried over from SupaNODE,
// may not be where WS2812B DIN actually lands here.
#define STATUS_LED_GPIO 48

#define PERIPH_PWR_EN_GPIO 47

// --- I2C bus pins ---
// I2C ("I-squared-C") is a 2-wire serial bus: SDA carries data, SCL is the
// clock. Every device on the bus shares these same two lines and is told
// apart by a 7-bit address (see i2c_scan() below, and AHT20_I2C_ADDR,
// MCP23017_I2C_ADDR_DEFAULT, OLED_I2C_ADDR in the respective driver files).
// Only one bus is set up here; AHT20, MCP23017 and the (optional) OLED all
// share it.
#define I2C_SDA_GPIO 8
#define I2C_SCL_GPIO 9

#define AHT20_I2C_ADDR      0x38
#define AHT20_CAL_MASK      0x18  // status bits [4:3] - both set means calibrated
#define AHT20_BUSY_BIT      0x80  // status bit [7] - 1 while a measurement is in progress

// --- SPI bus pins ---
// SPI is a 4-wire serial bus: SCK (clock), MOSI (controller->peripheral),
// MISO (peripheral->controller), and one CS ("chip select") line per
// device - unlike I2C, each device needs its own dedicated CS pin, which is
// how the controller picks which single device is "listening" for a given
// transaction. The ADS1220 ADC and the SD card share the same SCK/MOSI/MISO
// below, but each gets its own CS (ADC_CS_GPIO vs MMC_CS_GPIO).
#define SPI_HOST_ID   SPI2_HOST
#define SPI_SCK_GPIO  12
#define SPI_MOSI_GPIO 11
#define SPI_MISO_GPIO 13
#define ADC_CS_GPIO   14
#define ADC_DRDY_GPIO 15

// Nominal AVDD for display purposes only - the ADS1220 reads ratiometrically
// against the real AVDD rail (see ads1220_read_single_ended()), so the
// measurement itself doesn't depend on this being exact.
#define ADC_AVDD_NOMINAL_V 3.3f

#define MMC_CS_GPIO 2

#define SPWM_PHASE_A_GPIO 38
#define SPWM_PHASE_B_GPIO 39
#define SPWM_PHASE_C_GPIO 40
#define SPWM_DEFAULT_FREQ_HZ 50.0f

static const char *TAG = "main";

// Persistent handle used by ads1220_poll_task for the rest of the program's
// life - must be static, not a local in app_main, since app_main's own stack
// frame is reclaimed once it returns.
static ads1220_handle_t s_ads1220;

#if OLED_ENABLED
// Kept for any future screen updates (not currently redrawn by a task, but
// u8g2_t is the kind of handle worth keeping alive rather than local).
static u8g2_t s_u8g2;

// Latest sensor readings, for the OLED task to display. In the full lab
// project this lived in web_server.c behind a mutex, because the HTTP server
// task could read it at the same moment a sensor task was writing it. Here
// there's only one reader (oled_dashboard_task) and the two writers below,
// and a dashboard refresh is cosmetic - so if a read ever catches a value
// mid-write, the worst case is one frame showing a slightly stale number,
// not a crash. We skip the mutex for that reason. If you reuse this pattern
// for anything where a torn read would actually matter, protect it with a
// FreeRTOS mutex (see xSemaphoreCreateMutex() / xSemaphoreTake() /
// xSemaphoreGive(), or look at how the removed web_server.c used to do it).
static volatile float s_display_temp_c;
static volatile float s_display_humidity_pct;
static volatile float s_display_adc_v;
#endif

// Drives the 3.3V peripheral rail's enable pin high and gives it a moment to
// settle before anything tries to talk to a device on that rail. The AHT20
// datasheet (7.1) specifically requires >=100ms after its own power-up
// before it'll accept commands - it'll NACK (ESP_ERR_INVALID_RESPONSE) if
// addressed too early, even though it already ACKs a bare address probe.
static void init_peripheral_power(void)
{
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << PERIPH_PWR_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    gpio_set_level(PERIPH_PWR_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

// Sets up one I2C controller ("master") bus on the SDA/SCL pins above. This
// doesn't talk to any specific chip yet - it just configures the two GPIOs
// as an I2C bus so that AHT20/MCP23017/(OLED) can each be attached to it as
// a device afterwards (see e.g. i2c_master_bus_add_device() inside
// mcp23017_init()).
static i2c_master_bus_handle_t init_i2c_bus(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = I2C_SDA_GPIO,
        .scl_io_num = I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus_handle;
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));
    return bus_handle;
}

// Probes every valid 7-bit I2C address and logs the ones that ACK.
//
// I2C addresses are 7 bits (0x00-0x7F), but 0x00-0x02 and 0x78-0x7F are
// reserved for bus protocol features (general call, 10-bit addressing,
// etc.) rather than real devices, which is why the scan only covers
// 0x03-0x77. "ACK" (acknowledge) is the one bit a device pulls low after
// hearing its own address - no ACK means nothing on the bus claims that
// address. Handy for checking wiring/soldering before writing any
// device-specific code at all.
static void i2c_scan(i2c_master_bus_handle_t bus_handle)
{
    ESP_LOGI(TAG, "Scanning I2C bus (SDA=%d, SCL=%d)...", I2C_SDA_GPIO, I2C_SCL_GPIO);

    int found = 0;
    for (uint8_t addr = 0x03; addr <= 0x77; addr++) {
        if (i2c_master_probe(bus_handle, addr, 50) == ESP_OK) {
            ESP_LOGI(TAG, "  found device at 0x%02X", addr);
            found++;
        }
    }

    if (found == 0) {
        ESP_LOGW(TAG, "I2C scan: no devices responded");
    } else {
        ESP_LOGI(TAG, "I2C scan complete: %d device(s) found", found);
    }
}

// Sets up the shared SPI bus (SCK/MOSI/MISO only - no CS here, see the SPI
// bus pins comment above). Both the ADS1220 and the SD card attach to this
// same bus afterwards, each supplying their own CS pin when they do
// (ads1220_init()'s cs_gpio, sdcard_init()'s cs_gpio).
static void init_spi_bus(void)
{
    spi_bus_config_t bus_config = {
        .sclk_io_num = SPI_SCK_GPIO,
        .mosi_io_num = SPI_MOSI_GPIO,
        .miso_io_num = SPI_MISO_GPIO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST_ID, &bus_config, SPI_DMA_CH_AUTO));
}

// One-shot bring-up read, following the AHT20 datasheet's sensor reading
// process (section 7.4): check calibration status, init if needed, trigger
// a measurement, wait for it, then decode the 20-bit humidity/temperature
// fields packed across the 6 data bytes.
static esp_err_t aht20_read(i2c_master_bus_handle_t bus_handle, float *temperature_c, float *humidity_pct)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = AHT20_I2C_ADDR,
        .scl_speed_hz = 50000,
    };
    i2c_master_dev_handle_t dev;
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &dev);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t status_cmd = 0x71;
    uint8_t status = 0;
    err = i2c_master_transmit_receive(dev, &status_cmd, 1, &status, 1, pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AHT20: status read failed (%s)", esp_err_to_name(err));
        goto done;
    }

    if ((status & AHT20_CAL_MASK) != AHT20_CAL_MASK) {
        uint8_t init_cmd[3] = {0xBE, 0x08, 0x00};
        err = i2c_master_transmit(dev, init_cmd, sizeof(init_cmd), pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AHT20: init command failed (%s)", esp_err_to_name(err));
            goto done;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    uint8_t trigger_cmd[3] = {0xAC, 0x33, 0x00};
    err = i2c_master_transmit(dev, trigger_cmd, sizeof(trigger_cmd), pdMS_TO_TICKS(100));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "AHT20: trigger command failed (%s)", esp_err_to_name(err));
        goto done;
    }
    vTaskDelay(pdMS_TO_TICKS(80));

    // Datasheet: wait 80ms, then read and check the busy bit, continuing to
    // wait if it's still set. Poll a few more times with margin rather than
    // treating one read past 80ms as final.
    uint8_t data[6] = {0};
    for (int attempt = 0; ; attempt++) {
        err = i2c_master_receive(dev, data, sizeof(data), pdMS_TO_TICKS(100));
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "AHT20: data read failed on attempt %d (%s)", attempt, esp_err_to_name(err));
            goto done;
        }
        if (!(data[0] & AHT20_BUSY_BIT)) {
            break;
        }
        ESP_LOGI(TAG, "AHT20: still busy (status=0x%02X) on attempt %d", data[0], attempt);
        if (attempt >= 4) {
            err = ESP_ERR_TIMEOUT;
            goto done;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }

    // The AHT20 packs humidity and temperature together across 6 bytes as
    // two 20-bit fields (not byte-aligned!), encoded as a fraction of 2^20
    // (1048576) of their respective full-scale range. data[3]'s top nibble
    // belongs to humidity, its bottom nibble belongs to temperature - that's
    // the ">> 4" / "& 0x0F" split below.
    uint32_t raw_humidity = ((uint32_t)data[1] << 12) | ((uint32_t)data[2] << 4) | (data[3] >> 4);
    uint32_t raw_temp = (((uint32_t)data[3] & 0x0F) << 16) | ((uint32_t)data[4] << 8) | data[5];

    *humidity_pct = ((float)raw_humidity / 1048576.0f) * 100.0f;
    *temperature_c = ((float)raw_temp / 1048576.0f) * 200.0f - 50.0f;

done:
    i2c_master_bus_rm_device(dev);
    return err;
}

// Reads once a second, forever - each log line's own boot-uptime timestamp
// shows whether NACKs only happen right after power-up or keep recurring,
// which is the point of running this continuously rather than a one-shot.
// Deliberately left slower than the ADS1220 poll: a read here can take up to
// ~180ms worst-case (80ms conversion + busy-bit retries), and temperature/
// humidity don't change fast enough for sub-second polling to buy anything.
//
// This is a FreeRTOS task: a function that runs forever in its own stack,
// scheduled cooperatively alongside every other task (see xTaskCreate() in
// app_main() below, which is what actually starts this running). Tasks are
// how this whole program gets away with "doing several things at once" on
// one CPU core - the AHT20 read, the ADC poll, the status LED animation etc.
// are each their own task, and vTaskDelay() below is what lets the
// scheduler run other tasks while this one is waiting.
static void aht20_poll_task(void *arg)
{
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)arg;

    while (1) {
        float temp_c, humidity_pct;
        esp_err_t err = aht20_read(bus_handle, &temp_c, &humidity_pct);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "AHT20: %.2f degC, %.2f %%RH", temp_c, humidity_pct);
#if OLED_ENABLED
            s_display_temp_c = temp_c;
            s_display_humidity_pct = humidity_pct;
#endif
        } else {
            ESP_LOGW(TAG, "AHT20 read failed (%s)", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Continuous version of the boot-time ADS1220 probe read: keeps the latest
// potentiometer reading flowing to the serial log. Polled faster than AHT20
// (250ms vs 1s) - a single-shot conversion at the current data rate only
// takes ~50-60ms, so there's ample margin, and this is the value a student
// gets live feedback from while turning the pot.
static void ads1220_poll_task(void *arg)
{
    ads1220_handle_t *ads1220 = (ads1220_handle_t *)arg;

    while (1) {
        int32_t code;
        esp_err_t err = ads1220_read_single_ended(ads1220, 0, &code);  // AIN0 - the pot's wiper
        if (err == ESP_OK) {
            // The ADS1220 returns a signed 24-bit "code": the raw ADC
            // counts, sign-extended into an int32_t (see the comment in
            // ads1220_read_single_ended() for the sign-extension itself).
            // code/2^23 (8388608 = 2^23, the positive full-scale count for a
            // 24-bit signed value) is the wiper/AVDD ratio directly, since
            // this channel is configured ratiometric to AVDD with gain=1 -
            // multiplying by AVDD's nominal voltage here is just to turn
            // that ratio into a human-readable number for the log.
            float voltage = ((float)code / 8388608.0f) * ADC_AVDD_NOMINAL_V;
            ESP_LOGI(TAG, "ADS1220 AIN0: %.4f V", voltage);
#if OLED_ENABLED
            s_display_adc_v = voltage;
#endif
        } else {
            ESP_LOGW(TAG, "ADS1220 poll read failed (%s)", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

#if OLED_ENABLED
// Mirrors the latest sensor readings (and the current SPWM frequency) onto
// the OLED. There's no WiFi/IP address to show in this starter project, so
// an empty string is passed for that field - oled_draw_dashboard() already
// knows to print "(no wifi)" in that case.
static void oled_dashboard_task(void *arg)
{
    u8g2_t *u8g2 = (u8g2_t *)arg;

    while (1) {
        oled_draw_dashboard(u8g2, s_display_temp_c, s_display_humidity_pct, s_display_adc_v,
                             spwm_get_frequency(), "");

        // A full 128x64 flush is ~25-30ms of I2C at 400kHz, well within
        // budget to match the ADS1220's 250ms poll rate.
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}
#endif

void app_main(void)
{
    // NVS (non-volatile storage) is flash-backed key/value storage that
    // several ESP-IDF components rely on internally. It's good practice to
    // bring it up unconditionally near the top of app_main like this, even
    // though nothing in this starter project reads/writes it directly yet.
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    uint32_t flash_size = 0;
    esp_flash_get_size(NULL, &flash_size);

    ESP_LOGI(TAG, "Hello world from %s!", CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "%d CPU core(s), WiFi%s%s, silicon revision v%d.%d",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "%" PRIu32 "MB flash", flash_size / (1024 * 1024));

    init_peripheral_power();

    i2c_master_bus_handle_t i2c_bus = init_i2c_bus();
    i2c_scan(i2c_bus);

    // xTaskCreate(function, name, stack_size_in_bytes, arg_passed_to_function,
    //             priority, out_handle). NULL for the handle here just means
    //             "we don't need to reference this task again later" (no
    //             need to delete/suspend/resume it from elsewhere).
    xTaskCreate(aht20_poll_task, "aht20_poll", 4096, i2c_bus, tskIDLE_PRIORITY + 1, NULL);

    mcp23017_handle_t mcp23017;
    esp_err_t mcp23017_err = mcp23017_init(i2c_bus, MCP23017_I2C_ADDR_DEFAULT, &mcp23017);
    if (mcp23017_err == ESP_OK) {
        mcp23017_err = mcp23017_probe(&mcp23017);
    }
    if (mcp23017_err == ESP_OK) {
        ESP_LOGI(TAG, "MCP23017 at 0x%02X responding with sane register defaults", MCP23017_I2C_ADDR_DEFAULT);
    } else {
        ESP_LOGE(TAG, "MCP23017 probe failed (%s)", esp_err_to_name(mcp23017_err));
    }

#if OLED_ENABLED
    esp_err_t oled_err = oled_init(i2c_bus, &s_u8g2);
    if (oled_err == ESP_OK) {
        ESP_LOGI(TAG, "SSD1306 OLED responding, u8g2 initialized");
        xTaskCreate(oled_dashboard_task, "oled_dashboard", 4096, &s_u8g2, tskIDLE_PRIORITY + 1, NULL);
    } else {
        ESP_LOGE(TAG, "SSD1306 OLED init failed (%s)", esp_err_to_name(oled_err));
    }
#else
    ESP_LOGI(TAG, "OLED support disabled (OLED_ENABLED=0 in main.c) - skipping init");
#endif

    init_spi_bus();
    esp_err_t ads1220_err = ads1220_init(SPI_HOST_ID, ADC_CS_GPIO, ADC_DRDY_GPIO, &s_ads1220);
    if (ads1220_err == ESP_OK) {
        ads1220_err = ads1220_probe(&s_ads1220);
    }
    if (ads1220_err == ESP_OK) {
        ESP_LOGI(TAG, "ADS1220 responding with sane register defaults");
        xTaskCreate(ads1220_poll_task, "ads1220_poll", 4096, &s_ads1220, tskIDLE_PRIORITY + 1, NULL);
    } else {
        ESP_LOGE(TAG, "ADS1220 probe failed (%s)", esp_err_to_name(ads1220_err));
    }

    sdcard_handle_t sdcard;
    esp_err_t sdcard_err = sdcard_init(SPI_HOST_ID, MMC_CS_GPIO, &sdcard);
    if (sdcard_err == ESP_OK) {
        sdcard_err = sdcard_probe(&sdcard);
    }
    if (sdcard_err != ESP_OK) {
        ESP_LOGE(TAG, "SD card not responding (%s)", esp_err_to_name(sdcard_err));
    }

    esp_err_t spwm_err = spwm_init(SPWM_PHASE_A_GPIO, SPWM_PHASE_B_GPIO, SPWM_PHASE_C_GPIO, SPWM_DEFAULT_FREQ_HZ);
    if (spwm_err != ESP_OK) {
        ESP_LOGE(TAG, "SPWM init failed (%s)", esp_err_to_name(spwm_err));
    }

    // No WiFi station / web dashboard in this starter project - see the
    // big comment block at the top of this file for why. Everything above
    // still runs exactly as in the full lab project; sensor readings just
    // go out over the serial log (ESP_LOGI) instead of also being mirrored
    // to a web page.

    status_led_start(STATUS_LED_GPIO);
}
