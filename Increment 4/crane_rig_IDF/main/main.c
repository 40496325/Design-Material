// =============================================================================
// EngDes26 starter project - main.c  ->  REII crane rig, Increment 4
// =============================================================================
//
// REII CHANGES (Increment 4, built fresh from the provided starter project)
// -----------------------------------------------------------------------
// This is the provided starter's main.c with the nRF24L01+ link added per the
// ICD v1 IF10.0 operating sequence:
//   1. +3.3VP rail on (GPIO47), then every SPI chip select HIGH (nRF CSN
//      GPIO17, ADS1220 CS GPIO14, SD CS GPIO2) so nothing clashes on SPI2.
//   2. MCP23017 taken out of reset (PX_RESET = GPIO5) - its GPA0 (J3.3) is the
//      nRF CE line in ICD Table 31.
//   3. SPI2 bus on J1 (GPIO11/12/13), radio attached at 4 MHz, configured as
//      the primary receiver on 'REII2' ch 77 / 2 Mbps, CE HIGH (rf_link.c).
//   4. Typed commands on COM12 (rig_console.c) for the smoke test tool.
// All pins and link settings are in rig_config.h. Optional starter peripherals
// (AHT20, ADS1220, SD, SPWM) are switched by RIG_ENABLE_* in rig_config.h; the
// sensor tasks now log every RIG_SENSOR_LOG_MS instead of 4x per second so the
// radio's RIG,... lines stay readable.
//
// Original starter description follows.
// -----------------------------------------------------------------------
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
//   ads1220.c      - SPI 24-bit ADC (REII: load cell on J6, see loadcell.c)
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
#include "rig_config.h"
#include "rf_link.h"
#include "rig_console.h"
#include "rig_telemetry.h"
#include "loadcell.h"
#include "mcp_shared.h"      // DRIVE: shared MCP23017 owner (main/drive_electronics/)
#if RIG_ENABLE_DRIVE
#include "drive_control.h"   // DRIVE: hoist drive (main/drive_electronics/)
#endif
#if RIG_ENABLE_TROLLEY
#include "trolley_control.h" // TROLLEY: lateral drive + encoder (main/trolley/)
#endif
#if RIG_ENABLE_MAGNET
#include "magnet_control.h"  // MAGNET: electromagnet (main/magnet/)
#endif

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

// REII: GPIO48 -> STAT_LED -> WS2812B confirmed on ESP32S3Devboard.pdf, so the
// starter's TODO here is closed. All pins now come from rig_config.h.
#define STATUS_LED_GPIO RIG_STATUS_LED_GPIO

#define PERIPH_PWR_EN_GPIO RIG_PERIPH_PWR_EN_GPIO

// --- I2C bus pins ---
// I2C ("I-squared-C") is a 2-wire serial bus: SDA carries data, SCL is the
// clock. Every device on the bus shares these same two lines and is told
// apart by a 7-bit address (see i2c_scan() below, and AHT20_I2C_ADDR,
// MCP23017_I2C_ADDR_DEFAULT, OLED_I2C_ADDR in the respective driver files).
// Only one bus is set up here; AHT20, MCP23017 and the (optional) OLED all
// share it.
#define I2C_SDA_GPIO RIG_I2C_SDA_GPIO
#define I2C_SCL_GPIO RIG_I2C_SCL_GPIO

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
// REII: the nRF24L01+ is a third device on this same bus (CSN = GPIO17 on J1.1).
#define SPI_HOST_ID   RIG_SPI_HOST
#define SPI_SCK_GPIO  RIG_SPI_SCK_GPIO
#define SPI_MOSI_GPIO RIG_SPI_MOSI_GPIO
#define SPI_MISO_GPIO RIG_SPI_MISO_GPIO
#define ADC_CS_GPIO   RIG_ADC_CS_GPIO
#define ADC_DRDY_GPIO RIG_ADC_DRDY_GPIO

// Nominal AVDD for display purposes only - the ADS1220 reads ratiometrically
// against the real AVDD rail (see ads1220_read_single_ended()), so the
// measurement itself doesn't depend on this being exact.
#define ADC_AVDD_NOMINAL_V 3.3f

#define MMC_CS_GPIO RIG_MMC_CS_GPIO

// Starter SPWM (spwm.c, RIG_ENABLE_SPWM) - superseded by the drive electronics below.
#define SPWM_PHASE_A_GPIO 38
#define SPWM_PHASE_B_GPIO 39
#define SPWM_PHASE_C_GPIO 40
#define SPWM_DEFAULT_FREQ_HZ 50.0f

static const char *TAG = "main";

// Persistent handle used by ads1220_poll_task for the rest of the program's
// life - must be static, not a local in app_main, since app_main's own stack
// frame is reclaimed once it returns.
__attribute__((unused)) static ads1220_handle_t s_ads1220;

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

    // REII (ICD IF10.0 operating sequence): straight after the rail comes up,
    // hold every SPI chip select HIGH so no device on SPI2 drives MISO while
    // another is being addressed. The SPI driver takes each pin over again
    // when its device is added (ADS1220 / SD / nRF24).
    gpio_config_t cs_cfg = {
        .pin_bit_mask = (1ULL << RIG_NRF_CSN_GPIO) | (1ULL << ADC_CS_GPIO) | (1ULL << MMC_CS_GPIO),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&cs_cfg));
    gpio_set_level(RIG_NRF_CSN_GPIO, 1);
    gpio_set_level(ADC_CS_GPIO, 1);
    gpio_set_level(MMC_CS_GPIO, 1);

    // REII: MCP23017 /RESET is net PX_RESET on GPIO5. Pulse it so the expander
    // always starts from its documented reset state, then hold it released.
    gpio_config_t rst_cfg = {
        .pin_bit_mask = 1ULL << RIG_MCP_RESET_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst_cfg));
    gpio_set_level(RIG_MCP_RESET_GPIO, 0);
    vTaskDelay(pdMS_TO_TICKS(2));
    gpio_set_level(RIG_MCP_RESET_GPIO, 1);

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
__attribute__((unused)) static void aht20_poll_task(void *arg)
{
    i2c_master_bus_handle_t bus_handle = (i2c_master_bus_handle_t)arg;

    while (1) {
        float temp_c, humidity_pct;
        esp_err_t err = aht20_read(bus_handle, &temp_c, &humidity_pct);
        if (err == ESP_OK) {
            // REII: log once per RIG_SENSOR_LOG_MS (was every read) to keep the console readable.
            ESP_LOGI(TAG, "AHT20: %.2f degC, %.2f %%RH", temp_c, humidity_pct);
#if OLED_ENABLED
            s_display_temp_c = temp_c;
            s_display_humidity_pct = humidity_pct;
#endif
        } else {
            ESP_LOGW(TAG, "AHT20 read failed (%s)", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(RIG_SENSOR_LOG_MS));
    }
}

// Continuous version of the boot-time ADS1220 probe read: keeps the latest
// potentiometer reading flowing to the serial log. Polled faster than AHT20
// (250ms vs 1s) - a single-shot conversion at the current data rate only
// takes ~50-60ms, so there's ample margin, and this is the value a student
// gets live feedback from while turning the pot.
__attribute__((unused)) static void ads1220_poll_task(void *arg)
{
    ads1220_handle_t *ads1220 = (ads1220_handle_t *)arg;
    // REII: still sampled every 250 ms, but only logged every RIG_SENSOR_LOG_MS.
    const int log_every = RIG_SENSOR_LOG_MS / 250;
    int n = 0;

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
            if (n++ % log_every == 0) {
                ESP_LOGI(TAG, "ADS1220 AIN0: %.4f V", voltage);
            }
#if OLED_ENABLED
            s_display_adc_v = voltage;
#endif
        } else {
            ESP_LOGW(TAG, "ADS1220 poll read failed (%s)", esp_err_to_name(err));
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// =============================================================================
// DRIVE ELECTRONICS (EEII 1 VFD, ICD IF6.0) - added v4.5.0, 17 Nov 2026
// =============================================================================
// Code: main/drive_electronics/ (drive_control.c, drive_spwm.c, mcp_shared.c).
// This block is the ONE place to adjust the drive. Every value is tagged:
//   CONFIRMED - stated in ICD-001 Rev A or by EEII 1 / MCTR 2
//   ASSUMED   - not given anywhere yet; chosen here, change it when known
//
// Signal chain per phase: GPIO -> R -> 4N25 LED | 4N25 transistor + pull-up
// -> IR2104 IN (HO/LO with 520 ns built-in dead time) -> IGBT half bridge.
// "IN duty" below = fraction of each carrier period that IR2104 IN is HIGH
// (high-side IGBT on). The pin itself is the inverse (the opto inverts).
// =============================================================================
#if RIG_ENABLE_DRIVE
static void start_drive_electronics(void)
{
    const drive_cfg_t cfg = {
        // ---- SPWM outputs ---------------------------------------------------
        .spwm_gpio_a = 38,          // CONFIRMED J7.8 VFD_SPWM_A (ICD Table 19)
        .spwm_gpio_b = 39,          // CONFIRMED J7.7 VFD_SPWM_B
        .spwm_gpio_c = 40,          // CONFIRMED J7.6 VFD_SPWM_C
        .carrier_hz = 10000,        // CONFIRMED EEII 1, 17 Nov (ICD Rev A said 20 kHz -> ICD Rev B)
        .opto_inverts = true,       // CONFIRMED EEII 1, 17 Nov (4N25 output falls when the MCU pin is HIGH)
        .idle_in_level = 0,         // ASSUMED: stopped = IR2104 IN LOW on all legs (low-side IGBTs on,
                                    //   bootstrap caps charged). Pin is then HIGH (opto LED on).
        .duty_min = 0.15f,          // ASSUMED: shortest IN pulse 15 % of 100 us = 15 us. The opto output
        .duty_max = 0.85f,          //   rises slowly (EEII 1 scope photo, 10 Nov), so pulses much shorter
                                    //   than ~15 us may not reach the IR2104 threshold. Measure the opto
                                    //   output at 15 % and 85 % (DRIVE SQUARE A 15 / 85) and adjust.

        // ---- DRV_EN output --------------------------------------------------
        .drv_en_bit = 1,            // CONFIRMED J3.4 = MCP23017 GPA1 (ICD Table 19)
        .drv_en_active_high = true, // CONFIRMED EEII 1, 29 Oct: HIGH = DC-bus IGBT (via its opto) on

        // ---- Hoist brake output (MCTR 2, IF15.0) ----------------------------
        .brake_enabled = true,      // CONFIRMED pin J4.8 = MCP23017 GPB5 (ICD Table 38)
        .brake_bit = 5,
        .brake_release_high = true, // ASSUMED from ICD (HIGH = release); MCTR 2 to confirm
        .brake_delay_ms = 50,       // ASSUMED 50 ms (README D12). Order used: motor energised at f_start
                                    //   -> 50 ms -> brake released; on stop: brake clamped at f_start
                                    //   -> 50 ms -> SPWM off, DRV_EN off. MCTR 2 to confirm.

        // ---- Inputs (less strict for now) ------------------------------------
        .fault_enabled = true,      // J3.5 DRV_FAULT_IN = MCP23017 GPA2 (ICD Table 19)
        .fault_bit = 2,
        .fault_active_level = 0,    // ASSUMED active LOW (opto / open collector pulls low on a fault).
        .fault_pullup = true,       //   With the 100 k pull-up an unconnected pin reads "no fault".
        .oc_enabled = false,        // OFF (v4.6.0): J3.8 is "Free (Spare)" in the wiring document, which
        .oc_bit = 5,                //   is the final pinout - only stated pins are used. The wiring doc
        .oc_active_level = 0,       //   puts EEII 1's VFD current sensor on J6.7 (AN2, ADS1220 analog);
        .oc_pullup = true,          //   reading it needs the shared-ADC work (C4). Still open with EEII 1.
                                    // OPEN (EEII 1), v5.0: VFD current is NOT read. ICD IF6.0 (Tables 17-20)
                                    //   and EEII 1's SoW item 7 both say: shunt -> conditioned single-ended
                                    //   ANALOG voltage, linear with drive current, < 2.048 V, on J6.7 (AN2),
                                    //   reported as Table 34 bytes 12-13 (A x 10); SoW: the overcurrent fault
                                    //   is "inferred in firmware from commanded-vs-measured mismatch". EEII 1's
                                    //   "digital input to his own DAC" most likely means his board digitises
                                    //   on the HV side and a DAC rebuilds the analog voltage on the LV side.
                                    //   Until settled: vfd_current = 0 and fault b0 only from J3.5.

        // ---- Motion profile (ICD Table 20: 0-50 Hz, amplitude + frequency set speed)
        .f_max_hz = 50.0f,          // CONFIRMED ICD Table 20: |hoist_cmd| 100 % = 50 Hz
        .f_start_hz = 5.0f,         // ASSUMED: lowest running frequency (start and stop point)
        .f_base_hz = 50.0f,         // ASSUMED: V/f base frequency = motor rated 50 Hz
        .m_max = 0.70f,             // ASSUMED: modulation index at 50 Hz. 0.70 keeps the sine inside the
                                    //   15-85 % duty limits (0.5 +/- 0.35) so it is never clipped.
        .m_boost = 0.15f,           // ASSUMED: low-speed voltage boost (linear V/f would give m = 0.07 at 5 Hz)
        .ramp_hz_s = 25.0f,         // ASSUMED: 0 -> 50 Hz in 2 s (the dashboard S-curve ramps as well)
        .stop_ramp_hz_s = 100.0f,   // ASSUMED: lost link / soft stop: 50 -> 5 Hz in 0.45 s
        .reverse_dwell_ms = 500,    // ASSUMED: at rest this long before running the other way
                                    //   (EEII 1, 17 Nov: stop first, then swap phases)
        .cmd_deadband_pct = 2,      // ASSUMED: |hoist_cmd| < 2 % = stop
        .up_is_abc = true,          // ASSUMED: UP = phase order A-B-C, DOWN = A-C-B (B and C swapped).
                                    //   If the hoist lowers on an UP command, set false.
        .link_timeout_ms = RIG_LINK_TIMEOUT_MS,       // CONFIRMED ICD: 500 ms lost link -> safe hold
        .hoist_mm_s_at_fmax = RIG_MODEL_HOIST_MAX_MM_S, // ASSUMED: 80 mm/s at 50 Hz (SysRS PT2, 1.6 m in 20 s)
                                                        //   until MCTR 2 gives spool radius / gear ratio.
                                    // OPEN (MCTR 2). With their numbers:
                                    //   motor rpm at 50 Hz  n = 120 x 50 / poles x (1 - slip)   (4-pole, 5 %: 1425 rpm)
                                    //   hoist_mm_s_at_fmax  = 2 x pi x r_spool_mm x n / 60 / gear_ratio
                                    //   e.g. r = 20 mm, ratio 40:1 -> 2 x pi x 20 x 1425 / 60 / 40 = 75 mm/s
                                    //   Then check: DRIVE SINE 50 for 10 s, measure the rope travel / 10 s.

        // ---- Hoist travel limits from the position ESTIMATE (v5.0) ------------
        .hoist_limits = true,       // ASSUMED: armed only after the dashboard Zero Position (or DRIVE ZERO)
                                    //   with the hook at the BOTTOM; before that the hoist is not limited.
        .hoist_stroke_mm = (float)RIG_MODEL_HOIST_STROKE_MM, // ASSUMED: 1.6 m (SysRS PT2) - OPEN (MCTR 2)
        .hoist_limit_margin_mm = 100.0f, // ASSUMED: stopping distance at full speed with the 25 Hz/s ramp:
                                    //   (80 + 8) / 2 mm/s x 1.8 s = ~80 mm, so 100 mm margin each end.
                                    //   The estimate drifts (motor slip, rope layering): Zero Position
                                    //   again at the bottom whenever possible.
    };
    esp_err_t err = drive_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "drive electronics NOT running (%s)", esp_err_to_name(err));
    }
}
#endif

// =============================================================================
// TROLLEY (MCTR 1 lateral movement, ICD IF1.0) - added v4.6.0, 17 Nov 2026
// =============================================================================
// Code: main/trolley/ (trolley_control.c, trolley_io.c).
// Pins: ONLY "MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx" (final
// pinout). Header-pin -> GPIO from the board schematic: J7.9 = IO_A = GPIO18,
// J7.12 = IO_D = GPIO21.
// Tags: CONFIRMED (ICD / wiring doc / MCTR 1 / REII decision) or ASSUMED.
// =============================================================================
#if RIG_ENABLE_TROLLEY
static void start_trolley(void)
{
    const trolley_cfg_t cfg = {
        // ---- Pins (wiring document, MCTR 1) ---------------------------------
        .pwm_gpio = 18,             // CONFIRMED J7.9 (IO_A) LAT_PWM_ENA -> HW-095 ENA
        .pwm_hz = 5000,             // ASSUMED 5 kHz (wiring doc / ICD allow 1-10 kHz)
        .in1_bit = 1,               // CONFIRMED J4.4 = MCP23017 GPB1 MOT_IN1
        .in2_bit = 2,               // CONFIRMED J4.5 = MCP23017 GPB2 MOT_IN2
        .mirror_in34 = true,        // CONFIRMED (REII 17 Nov, D11): IN3 / IN4 copy IN1 / IN2, so it works
        .in3_bit = 3,               //   whether MCTR 1 wires J4.6 / J4.7 (GPB3 / GPB4) or jumpers
        .in4_bit = 4,               //   IN3 / IN4 to IN1 / IN2 on the HW-095 (ENB jumpered to ENA)
        .enc_gpio = 21,             // CONFIRMED J7.12 (IO_D) ENC_A, one encoder, channel A only
        .enc_pullup = false,        // ASSUMED: encoder output arrives already stepped down to 3.3 V
                                    //   (wiring doc: "3.3 V level-shifted"). OPEN with MCTR 1: if the
                                    //   encoder is open collector, fit 4.7 k to 3.3 V (or set true).
        .enc_glitch_ns = 1000,      // ASSUMED: ignore pulses < 1 us (real edges are >= ~50 us apart)

        // ---- Mechanics --------------------------------------------------------
        .wheel_diameter_mm = 60.0f,       // CONFIRMED MCTR 1, 29 Oct
        .counts_per_wheel_rev = 1050.0f,  // CONFIRMED ICD Table 15 / wiring doc (rising edges per wheel turn).
                                          //   MCTR 1's note implied ~467; closed 17 Nov ("doesn't matter").
                                          //   -> pi * 60 / 1050 = 0.1795 mm per count
        .rail_length_mm = 20000.0f,       // CONFIRMED 20 m (17 Nov)
        .limit_margin_mm = 200.0f,        // ASSUMED: stop 0.2 m before each end
        .slow_zone_mm = 1000.0f,          // ASSUMED: last 1 m before an armed limit ...
        .slow_zone_duty_pct = 30,         //   ... at most 30 % duty, so it can stop inside the 0.2 m margin
        .soft_limits = true,              // ASSUMED: armed only after Zero Position with the trolley at the
                                          //   end it reaches on a "-" (REV) command. Which end is "+" does
                                          //   not matter (closed 17 Nov). TROLLEY LIMITS 0 disables.

        // ---- Motion profile ---------------------------------------------------
        .slew_pct_s = 100.0f,       // ASSUMED: ENA duty 0 -> 100 % in 1 s (the dashboard S-curve ramps too)
        .stop_slew_pct_s = 250.0f,  // ASSUMED: lost link / soft stop / rail limit: 100 -> 0 % in 0.4 s
        .min_run_duty_pct = 0,      // CONFIRMED ICD: duty = |trolley_cmd| %. Raise if the motors stall at
                                    //   low duty (test with TROLLEY RUN 10 FWD, 15, 20 ...)
        .max_duty_pct = 100,        // ASSUMED: no cap
        .cmd_deadband_pct = 2,      // ASSUMED: |trolley_cmd| < 2 % = stop
        .reverse_dwell_ms = 300,    // ASSUMED: coast >= 0.3 s at zero duty before reversing ...
        .standstill_ms = 100,       //   ... and until the encoder shows no counts for 0.1 s (stopped)
        .reverse_max_wait_ms = 3000,//   ... but never wait longer than 3 s (encoder missing)
        .link_timeout_ms = RIG_LINK_TIMEOUT_MS,   // CONFIRMED ICD: 500 ms lost link -> stop

        // ---- Encoder check ------------------------------------------------------
        .enc_check = true,          // ASSUMED: no counts for 1 s while driving at >= 20 % -> fault b5
        .enc_check_min_duty_pct = 20,
        .enc_check_ms = 1000,
    };
    esp_err_t err = trolley_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "trolley NOT running (%s)", esp_err_to_name(err));
    }
}
#endif

// =============================================================================
// MAGNET (EEII 2 load attachment, ICD IF12.0) - added v4.7.0, 18 Nov 2026
// =============================================================================
// Code: main/magnet/ (magnet_control.c). Current sensor read through the shared
// ADS1220 in loadcell.c (loadcell_set_aux).
// Pins: ONLY "MCU_Wiring_Diagram_and_Header_Pin_Allocation (1).docx". J7.5 is
// silkscreen "IO4" = GPIO41 (board map: J7.3-J7.8 = GPIO 1, 42, 41, 40, 39, 38),
// NOT GPIO4 (GPIO4 is J7.11 / IO_C).
// Tags: CONFIRMED (ICD / wiring doc / EEII 2 18 Nov) or ASSUMED.
//
// EEII 2 bench test 18 Nov (video): 15 kHz, 99 % duty, 5 V supply CURRENT-LIMITED at
// 2.53 A, sensor 2.32 V; at 10 % duty -> 0.00 A, sensor 2.57 V.
// EEII 2, 18 Nov (final): magnet supply = 12 V, 99 % stays on 12 V, holding = 7.5 A,
// coil resistance ~1 ohm -> on 12 V at 99 %: ~11.9 V / ~1 ohm = up to ~11.9 A (less
// with MOSFET + wiring resistance), ~140 W in the coils.
// FINAL PASS 18 Nov: later changes are made only in this block.
// =============================================================================
#if RIG_ENABLE_MAGNET
static void start_magnet(void)
{
    const magnet_cfg_t cfg = {
        // ---- Outputs -------------------------------------------------------------
        .pwm_gpio = 41,              // CONFIRMED J7.5 (silkscreen IO4) MAG_PWM
        .pwm_hz = 15000,             // CONFIRMED EEII 2, 18 Nov (wiring doc / ICD: up to 20 kHz)
        .on_duty_pct = 99.0f,        // CONFIRMED EEII 2, 18 Nov: 99 % on the 12 V supply. With ~1 ohm coils this
                                     //   is up to ~11.9 A / ~140 W (above the ICD 7.5 A). If the coils get hot
                                     //   or over_current_warn_a trips: ~65 % gives ~7.7 A (12 x 0.65 / 1 ohm).
        .release_duty_pct = 10.0f,   // CONFIRMED EEII 2, 18 Nov: 10 % disengages (0.00 A measured - but on a
                                     //   current-limited 5 V supply). On 12 V, 10 % = ~1.2 V -> up to ~1.2 A
                                     //   through ~1 ohm: may keep a light plate stuck. If MAGNET shows
                                     //   release_i_high=1 or the load won't drop: set 0.0f here.
        .boot_duty_pct = 0.0f,       // ASSUMED: 0 % until the first command (definitely off)
        .logic_bit = 3,              // CONFIRMED J3.6 = MCP23017 GPA3 MAG_LOGIC
        .logic_on_high = true,       // CONFIRMED EEII 2, 18 Nov: enable, HIGH while engaging
        .logic_lead_ms = 5,          // ASSUMED: MAG_LOGIC HIGH 5 ms before the PWM goes to the ON duty

        // ---- Current sensor: J6.8 (AN3) -> ADS1220 AIN3, shared with the load cell
        .sense_enabled = true,       // CONFIRMED EEII 2: no divider needed. The output FALLS with current
                                     //   (2.57 V at 0 A -> 2.32 V at 2.53 A): 7.5 A -> ~1.83 V, 11.9 A -> ~1.39 V,
                                     //   all inside the 0-3.3 V range of J6.8.
        .adc_channel = 3,            // CONFIRMED J6.8 = AN3 = ADS1220 AIN3
        .aux_period_ms = 500,        // ASSUMED: current read every 0.5 s; load cell drops from 20 to ~12-14 SPS (1000 = ~16 SPS)
        .avdd_v = 3.30f,             // ASSUMED: ADS1220 reference = AVDD = +3.3VP. Measure J6.1 to J6.2/GND
        .zero_v = 2.57f,             // CONFIRMED EEII 2: 2.57 V at 0 A (10 % duty). Start value only:
        .auto_zero = true,           //   re-measured automatically after the magnet has been off
        .auto_zero_after_ms = 2000,  //   for 2 s (ASSUMED), or by hand with MAGNET ZERO
        .auto_zero_window_v = 0.05f, // ASSUMED: auto-zero follows only drift < 0.05 V (~0.5 A), so a real
                                     //   current at the 10 % release duty is shown, not zeroed away
        .sens_v_per_a = -0.0988f,    // CONFIRMED from EEII 2's measurement: (2.32 - 2.57) V / 2.53 A.
                                     //   ~100 mV/A = an ACS712-20A-type (+-20 A) sensor, fitted with current
                                     //   entering the "-" side (hence negative). The graph EEII 2 sent
                                     //   (~185 mV/A) is the +-5 A version: if THAT part is fitted it clips
                                     //   near 5 A and 7.5 A is never seen. Only calibrated at 2.53 A (bench
                                     //   current limit): re-check at full current with MAGNET CAL <amps>.
        .sensor_min_v = 0.30f,       // ASSUMED: outside 0.3-3.2 V = sensor unplugged / unpowered ->
        .sensor_max_v = 3.20f,       //   fault b4 while engaged (instead of a false "holding")

        // ---- Holding check -------------------------------------------------------
        .hold_check = true,
        .hold_threshold_a = 7.5f,    // CONFIRMED EEII 2, 18 Nov (final): holding = 7.5 A. On 12 V with ~1 ohm
                                     //   coils ~11.9 A is expected, so 7.5 A is reached. On a current-limited
                                     //   bench supply (2.53 A) use MAGNET THRESHOLD 2.
        .hold_timeout_ms = 2000,     // ASSUMED: below the threshold 2 s after engaging = fault b4 (indication only,
                                     //   the magnet is never switched off by the firmware)

        // ---- Warnings (console + status flags only) --------------------------------
        .release_warn_a = 0.5f,      // ASSUMED: > 0.5 A while released = load may not let go (see release_duty_pct)
        .over_current_warn_a = 10.0f,// ASSUMED: > 10 A while engaged = above the ICD 7.5 A by a margin; check
                                     //   coil heating / supply (12 V supply must deliver ~12 A)
    };
    esp_err_t err = magnet_start(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "magnet NOT fully running (%s)", esp_err_to_name(err));
    }
}
#endif

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

    ESP_LOGI(TAG, "%s v%s on %s", RIG_FW_NAME, RIG_FW_VERSION, CONFIG_IDF_TARGET);
    ESP_LOGI(TAG, "%d CPU core(s), WiFi%s%s, silicon revision v%d.%d",
             chip_info.cores,
             (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
             (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "",
             chip_info.revision / 100, chip_info.revision % 100);
    ESP_LOGI(TAG, "%" PRIu32 "MB flash", flash_size / (1024 * 1024));

    // REII: rail on + all SPI chip selects HIGH + MCP23017 out of reset.
    init_peripheral_power();

    // REII: console first, so every later boot line can be followed and so the
    // smoke test can talk to the board as soon as the radio is up.
    rig_console_start();

    i2c_master_bus_handle_t i2c_bus = init_i2c_bus();
    i2c_scan(i2c_bus);

#if RIG_ENABLE_AHT20
    // xTaskCreate(function, name, stack_size_in_bytes, arg_passed_to_function,
    //             priority, out_handle). NULL for the handle here just means
    //             "we don't need to reference this task again later" (no
    //             need to delete/suspend/resume it from elsewhere).
    xTaskCreate(aht20_poll_task, "aht20_poll", 4096, i2c_bus, tskIDLE_PRIORITY + 1, NULL);
#endif

    // REII: the MCP23017 handle must outlive app_main - it drives nRF CE on J3.3 (GPA0).
    static mcp23017_handle_t mcp23017;
    esp_err_t mcp23017_err = mcp23017_init(i2c_bus, MCP23017_I2C_ADDR_DEFAULT, &mcp23017);
    if (mcp23017_err == ESP_OK) {
        mcp23017_err = mcp23017_probe(&mcp23017);
    }
    if (mcp23017_err == ESP_OK) {
        ESP_LOGI(TAG, "MCP23017 at 0x%02X responding with sane register defaults", MCP23017_I2C_ADDR_DEFAULT);
    } else {
        ESP_LOGE(TAG, "MCP23017 probe failed (%s) - nRF CE on J3.3 will NOT be driven", esp_err_to_name(mcp23017_err));
    }
    // DRIVE: from here on every MCP23017 pin (CE, DRV_EN, brake, inputs) goes through mcp_shared.
    mcp_shared_init(mcp23017_err == ESP_OK ? &mcp23017 : NULL);
#if RIG_ENABLE_DRIVE
    // DRIVE: as early as possible, so DRV_EN is held LOW and the brake clamped from boot.
    start_drive_electronics();
#endif
#if RIG_ENABLE_TROLLEY
    // TROLLEY: IN1-IN4 LOW (coast) and ENA 0 % from boot.
    start_trolley();
#endif
#if RIG_ENABLE_MAGNET
    // MAGNET: MAG_LOGIC LOW and MAG_PWM 0 % from boot; current read via the load-cell task once it runs.
    start_magnet();
#endif

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

#if RIG_ENABLE_ADS1220
    esp_err_t ads1220_err = ads1220_init(SPI_HOST_ID, ADC_CS_GPIO, ADC_DRDY_GPIO, &s_ads1220);
    if (ads1220_err == ESP_OK) {
        ads1220_err = ads1220_probe(&s_ads1220);
    }
    if (ads1220_err == ESP_OK) {
        ESP_LOGI(TAG, "ADS1220 responding with sane register defaults");
#if RIG_ENABLE_LOADCELL && RIG_LOADCELL_SOURCE == RIG_LOADCELL_ADS1220
        // REII (ICD IF13.0): the ADS1220 reads the bare load-cell bridge on J6 (AIN0-AIN1).
        esp_err_t lc_err = loadcell_start(&s_ads1220);
        if (lc_err != ESP_OK) {
            ESP_LOGE(TAG, "load cell start failed (%s) - load_cell telemetry stays 0", esp_err_to_name(lc_err));
        }
#else
        xTaskCreate(ads1220_poll_task, "ads1220_poll", 4096, &s_ads1220, tskIDLE_PRIORITY + 1, NULL);
#endif
    } else {
        ESP_LOGE(TAG, "ADS1220 probe failed (%s)", esp_err_to_name(ads1220_err));
    }
#else
    ESP_LOGI(TAG, "ADS1220 disabled (RIG_ENABLE_ADS1220=0) - CS GPIO%d held HIGH", ADC_CS_GPIO);
#endif

#if RIG_ENABLE_LOADCELL && RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
    // REII (ICD IF13.0 alternative): load cell on an HX711 board, DAT J7.11 / CLK J7.4.
    esp_err_t hx_err = loadcell_start(NULL);
    if (hx_err != ESP_OK) {
        ESP_LOGE(TAG, "HX711 load cell start failed (%s) - load_cell telemetry stays 0", esp_err_to_name(hx_err));
    }
#endif

#if RIG_ENABLE_SDCARD
    sdcard_handle_t sdcard;
    esp_err_t sdcard_err = sdcard_init(SPI_HOST_ID, MMC_CS_GPIO, &sdcard);
    if (sdcard_err == ESP_OK) {
        sdcard_err = sdcard_probe(&sdcard);
    }
    if (sdcard_err != ESP_OK) {
        ESP_LOGE(TAG, "SD card not responding (%s)", esp_err_to_name(sdcard_err));
    }
#else
    ESP_LOGI(TAG, "SD card disabled (RIG_ENABLE_SDCARD=0) - CS GPIO%d held HIGH", MMC_CS_GPIO);
#endif

#if RIG_ENABLE_SPWM
    esp_err_t spwm_err = spwm_init(SPWM_PHASE_A_GPIO, SPWM_PHASE_B_GPIO, SPWM_PHASE_C_GPIO, SPWM_DEFAULT_FREQ_HZ);
    if (spwm_err != ESP_OK) {
        ESP_LOGE(TAG, "SPWM init failed (%s)", esp_err_to_name(spwm_err));
    }
#else
    ESP_LOGI(TAG, "SPWM disabled (RIG_ENABLE_SPWM=0) - J7.6-J7.8 idle");
#endif

    // No WiFi station / web dashboard in this starter project - see the
    // big comment block at the top of this file for why. (The nRF24 below is
    // a separate 2.4 GHz radio on J1, not the ESP32-S3's own WiFi radio.)

    status_led_start(STATUS_LED_GPIO);

    // REII: the wireless link to the ground station (ICD IF10.0).
    esp_err_t rf_err = rf_link_start(mcp23017_err == ESP_OK ? &mcp23017 : NULL);
    if (rf_err != ESP_OK) {
        ESP_LOGE(TAG, "radio link NOT running (%s) - type INFO / REINIT on this port after fixing wiring",
                 esp_err_to_name(rf_err));
    }
}
