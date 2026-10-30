#include "loadcell.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs.h"

#include "rig_config.h"
#include "rig_telemetry.h"

static const char *TAG = "loadcell";

#define FULL_SCALE_CODE 8388608.0f      // 2^23
#define SAT_CODE        8300000         // ~99 % of full scale
#define NVS_NS          "loadcell"

typedef enum { REQ_NONE = 0, REQ_TARE, REQ_CAL, REQ_RESET } req_t;

static ads1220_handle_t *s_adc;
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
static portMUX_TYPE s_hx_mux = portMUX_INITIALIZER_UNLOCKED;
#define SRC_NAME "HX711"
// HX711 channel A, gain 128: full scale = +/-0.5 * AVDD / 128, and the bridge is excited
// from the board's own E+ (~AVDD), so code / (2^23 * 256) is the bridge ratio V/V.
#define COUNTS_PER_VV (FULL_SCALE_CODE * 256.0f)
#else
#define SRC_NAME "ADS1220"
// ADS1220 gain 128, VREF = AVDD = excitation: code / (2^23 * 128) is the bridge ratio V/V.
#define COUNTS_PER_VV (FULL_SCALE_CODE * (float)(1 << RIG_LOADCELL_GAIN_LOG2))
#endif

// ---------------------------------------------------------------------------
// HX711 (2-wire: DAT = DOUT, CLK = PD_SCK) on direct ESP32 GPIOs
// ---------------------------------------------------------------------------
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
static esp_err_t hx711_init(void)
{
    gpio_config_t sck = {.pin_bit_mask = 1ULL << RIG_HX711_SCK_GPIO, .mode = GPIO_MODE_OUTPUT};
    gpio_config_t dout = {.pin_bit_mask = 1ULL << RIG_HX711_DOUT_GPIO, .mode = GPIO_MODE_INPUT,
                          .pull_up_en = GPIO_PULLUP_ENABLE};   // unplugged DAT reads high = "never ready"
    esp_err_t err = gpio_config(&sck);
    if (err == ESP_OK) err = gpio_config(&dout);
    gpio_set_level(RIG_HX711_SCK_GPIO, 0);   // CLK high for > 60 us would power the HX711 down
    return err;
}

static esp_err_t hx711_read(int timeout_ms, int32_t *out_code)
{
    const int64_t deadline = esp_timer_get_time() + (int64_t)timeout_ms * 1000;
    while (gpio_get_level(RIG_HX711_DOUT_GPIO) != 0) {   // DOUT low = conversion ready
        if (esp_timer_get_time() > deadline) return ESP_ERR_TIMEOUT;
        vTaskDelay(1);
    }
    uint32_t v = 0;
    // 24 data bits MSB first + 1 extra pulse = channel A, gain 128 for the next conversion.
    // Interrupts held off for ~60 us so no CLK-high phase can stretch past 60 us.
    portENTER_CRITICAL(&s_hx_mux);
    for (int i = 0; i < 24; i++) {
        gpio_set_level(RIG_HX711_SCK_GPIO, 1);
        esp_rom_delay_us(1);
        v = (v << 1) | (uint32_t)gpio_get_level(RIG_HX711_DOUT_GPIO);
        gpio_set_level(RIG_HX711_SCK_GPIO, 0);
        esp_rom_delay_us(1);
    }
    gpio_set_level(RIG_HX711_SCK_GPIO, 1);
    esp_rom_delay_us(1);
    gpio_set_level(RIG_HX711_SCK_GPIO, 0);
    portEXIT_CRITICAL(&s_hx_mux);
    if (v & 0x800000) v |= 0xFF000000;
    *out_code = (int32_t)v;
    return ESP_OK;
}
#endif

// ---------------------------------------------------------------------------
// Source-independent access
// ---------------------------------------------------------------------------
static esp_err_t adc_read(int timeout_ms, int32_t *code)
{
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
    return hx711_read(timeout_ms, code);
#else
    return ads1220_read_continuous(s_adc, timeout_ms, code);
#endif
}
static loadcell_status_t st;
static SemaphoreHandle_t s_lock;
static volatile req_t s_req;
static volatile float s_req_kg;
static volatile uint32_t s_watch_until_ms;

static float default_kg_per_code(void)
{
    // code/2^23 = Vin / (VREF/gain) and VREF = Vexc  ->  Vin/Vexc = code / (2^23 * gain)
    // kg = (Vin/Vexc) / (sensitivity mV/V / 1000) * rated_kg
    return RIG_LOADCELL_RATED_KG / ((RIG_LOADCELL_SENS_MV_V / 1000.0f) * COUNTS_PER_VV);
}

static void nvs_load(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) != ESP_OK) return;
    float k;
    int32_t tare;
    size_t len = sizeof(k);
    if (nvs_get_blob(h, "kgpc", &k, &len) == ESP_OK && len == sizeof(k) && isfinite(k) && k != 0.0f) {
        st.kg_per_code = k;
        st.calibrated = true;
    }
    if (nvs_get_i32(h, "tare", &tare) == ESP_OK) st.tare = tare;
    nvs_close(h);
}

static void nvs_save(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (st.calibrated) {
        nvs_set_blob(h, "kgpc", &st.kg_per_code, sizeof(st.kg_per_code));
    } else {
        nvs_erase_key(h, "kgpc");
    }
    nvs_set_i32(h, "tare", st.tare);
    nvs_commit(h);
    nvs_close(h);
}

void loadcell_get(loadcell_status_t *out)
{
    if (!s_lock) {
        memset(out, 0, sizeof(*out));
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = st;
    xSemaphoreGive(s_lock);
}

void loadcell_print(const char *tag)
{
    loadcell_status_t s;
    loadcell_get(&s);
    printf("RIG,%s,src=" SRC_NAME ",running=%d,valid=%d,saturated=%d,raw=%ld,filtered=%.0f,mv_per_v=%.4f,kg=%.2f,load_x10=%d,"
           "tare=%ld,kg_per_code=%.4e,calibrated=%d,samples=%lu,errors=%lu\n",
           tag, s.running, s.valid, s.saturated, (long)s.raw, s.filtered, s.mv_per_v, s.kg,
           s.kg <= 0 ? 0 : (int)lroundf(s.kg * 10.0f), (long)s.tare, s.kg_per_code, s.calibrated,
           (unsigned long)s.samples, (unsigned long)s.errors);
}

void loadcell_command(const char *a1, const char *a2)
{
    if (!s_lock) {
        printf("ERR,load cell not running (RIG_ENABLE_LOADCELL / ADS1220 probe)\n");
        return;
    }
    if (!a1) {
        loadcell_print("LOAD");
        printf("OK,LOAD\n");
    } else if (!strcmp(a1, "TARE")) {
        s_req = REQ_TARE;
        printf("OK,LOAD,TARE,started - keep the hook unloaded for about 2 s\n");
    } else if (!strcmp(a1, "CAL")) {
        float kg = a2 ? strtof(a2, NULL) : 0.0f;
        if (!(kg > 0.0f && kg < 100.0f)) {
            printf("ERR,usage: LOAD CAL <known mass in kg, e.g. 5.0>\n");
            return;
        }
        s_req_kg = kg;
        s_req = REQ_CAL;
        printf("OK,LOAD,CAL,started with %.3f kg - keep it hanging still for about 2 s\n", kg);
    } else if (!strcmp(a1, "WATCH")) {
        int s = a2 ? atoi(a2) : 10;
        if (s < 1 || s > 600) s = 10;
        s_watch_until_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS) + (uint32_t)s * 1000;
        printf("OK,LOAD,WATCH,%d s at 5 Hz\n", s);
    } else if (!strcmp(a1, "RESET")) {
        s_req = REQ_RESET;
        printf("OK,LOAD,RESET,calibration back to the rig_config.h default\n");
    } else {
        printf("ERR,usage: LOAD | LOAD TARE | LOAD CAL <kg> | LOAD WATCH [s] | LOAD RESET\n");
    }
}

// Averages n conversions (blocking, inside the load-cell task only).
static bool average_codes(int n, float *out)
{
    double sum = 0;
    int got = 0;
    for (int i = 0; i < n * 2 && got < n; i++) {
        int32_t c;
        if (adc_read(300, &c) == ESP_OK) {
            sum += c;
            got++;
        }
    }
    if (got < n) return false;
    *out = (float)(sum / got);
    return true;
}

static void handle_request(void)
{
    req_t r = s_req;
    if (r == REQ_NONE) return;
    s_req = REQ_NONE;
    float avg;
    if (r == REQ_RESET) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        st.kg_per_code = default_kg_per_code();
        st.calibrated = false;
        xSemaphoreGive(s_lock);
        nvs_save();
        loadcell_print("LOAD");
        return;
    }
    if (!average_codes(RIG_LOADCELL_TARE_SAMPLES, &avg)) {
        printf("RIG,WARN,load cell %s failed - no conversions from the " SRC_NAME "\n", r == REQ_TARE ? "tare" : "calibration");
        return;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (r == REQ_TARE) {
        st.tare = (int32_t)lroundf(avg);
    } else {
        const float delta = avg - (float)st.tare;
        if (fabsf(delta) < 200.0f) {
            xSemaphoreGive(s_lock);
            printf("RIG,WARN,load cell calibration rejected - the reading barely changed (%.0f codes). "
                   "Is the mass hanging on the cell and was LOAD TARE done unloaded?\n", delta);
            return;
        }
        st.kg_per_code = s_req_kg / delta;   // sign follows the wiring, so a reversed bridge still reads positive
        st.calibrated = true;
    }
    st.valid = false;   // refill the filter with post-change samples
    xSemaphoreGive(s_lock);
    nvs_save();
    printf("RIG,LOAD_%s,done\n", r == REQ_TARE ? "TARE" : "CAL");
    loadcell_print("LOAD");
}

static void loadcell_task(void *arg)
{
    (void)arg;
    float window[RIG_LOADCELL_AVG];
    int idx = 0, filled = 0;
    uint32_t next_watch = 0;

    if (RIG_LOADCELL_TARE_AT_BOOT) {
        vTaskDelay(pdMS_TO_TICKS(300));   // let the bridge and the digital filter settle
        s_req = REQ_TARE;
    }
    for (;;) {
        handle_request();
        if (st.valid == false && filled >= RIG_LOADCELL_AVG) filled = 0;   // restart after tare/cal

        int32_t code;
        esp_err_t err = adc_read(300, &code);
        if (err != ESP_OK) {
            xSemaphoreTake(s_lock, portMAX_DELAY);
            st.errors++;
            st.valid = false;
            xSemaphoreGive(s_lock);
            rig_tlm_set_load(0.0f, false, false);
            if ((st.errors % 20) == 1) {
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
                printf("RIG,WARN,HX711 not answering (%s) - check DAT J7.11, CLK J7.4, VCC J3.11 (3.3 V), GND J3.1\n",
                       esp_err_to_name(err));
#else
                printf("RIG,WARN,load cell read failed (%s)\n", esp_err_to_name(err));
#endif
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        window[idx] = (float)code;
        idx = (idx + 1) % RIG_LOADCELL_AVG;
        if (filled < RIG_LOADCELL_AVG) filled++;
        float sum = 0;
        for (int i = 0; i < filled; i++) sum += window[i];

        xSemaphoreTake(s_lock, portMAX_DELAY);
        st.raw = code;
        st.samples++;
        st.filtered = sum / (float)filled;
        st.saturated = abs(code) >= SAT_CODE;
        st.mv_per_v = st.filtered / COUNTS_PER_VV * 1000.0f;
        st.kg = (st.filtered - (float)st.tare) * st.kg_per_code;
        st.valid = filled >= RIG_LOADCELL_AVG;
        const float kg = st.kg;
        const bool valid = st.valid, sat = st.saturated;
        xSemaphoreGive(s_lock);
        rig_tlm_set_load(kg, valid, sat);

        const uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        if ((int32_t)(s_watch_until_ms - now) > 0 && (int32_t)(now - next_watch) >= 0) {
            next_watch = now + 200;
            loadcell_print("LOAD");
        }
    }
}

esp_err_t loadcell_start(ads1220_handle_t *adc)
{
    s_adc = adc;
    memset(&st, 0, sizeof(st));
    st.kg_per_code = default_kg_per_code();
    nvs_load();
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
    (void)adc;
    esp_err_t err = hx711_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HX711 GPIO setup failed (%s)", esp_err_to_name(err));
        return err;
    }
    int32_t first;
    if (hx711_read(500, &first) != ESP_OK) {
        ESP_LOGW(TAG, "HX711 not answering yet - check DAT J7.11 (GPIO%d), CLK J7.4 (GPIO%d), VCC J3.11, GND J3.1. "
                      "Keeps retrying.", RIG_HX711_DOUT_GPIO, RIG_HX711_SCK_GPIO);
    }
#else
    if (!adc) return ESP_ERR_INVALID_ARG;
    esp_err_t err = ads1220_configure_bridge(adc, RIG_LOADCELL_GAIN_LOG2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ADS1220 bridge configuration failed (%s)", esp_err_to_name(err));
        return err;
    }
#endif
    st.running = true;
    s_lock = xSemaphoreCreateMutex();
    xTaskCreate(loadcell_task, "loadcell", 4096, NULL, tskIDLE_PRIORITY + 3, NULL);
#if RIG_LOADCELL_SOURCE == RIG_LOADCELL_HX711
    ESP_LOGI(TAG, "load cell via HX711 (DAT J7.11 GPIO%d, CLK J7.4 GPIO%d, ch A gain 128), %s scale %.4e kg/code%s",
             RIG_HX711_DOUT_GPIO, RIG_HX711_SCK_GPIO, st.calibrated ? "calibrated" : "datasheet", st.kg_per_code,
             RIG_LOADCELL_TARE_AT_BOOT ? ", taring now - keep the hook unloaded" : "");
#else
    ESP_LOGI(TAG, "load cell on J6 (AIN0-AIN1, gain %d, 20 SPS, 50 Hz reject), %s scale %.4e kg/code%s",
             1 << RIG_LOADCELL_GAIN_LOG2, st.calibrated ? "calibrated" : "datasheet", st.kg_per_code,
             RIG_LOADCELL_TARE_AT_BOOT ? ", taring now - keep the hook unloaded" : "");
#endif
    return ESP_OK;
}
