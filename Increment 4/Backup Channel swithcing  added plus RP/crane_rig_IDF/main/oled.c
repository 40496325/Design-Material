#include "oled.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>

#define OLED_I2C_ADDR 0x3C

// Generous upper bound: a full 128x64 buffer flush is at most 1024 bytes of
// pixel data (128 * 64 / 8) plus a handful of command/address bytes.
#define OLED_XFER_BUF_MAX 1200

static i2c_master_dev_handle_t s_i2c_dev;
static uint8_t s_xfer_buf[OLED_XFER_BUF_MAX];
static size_t s_xfer_len;

// u8x8 byte-transport callback. A single logical u8g2 transfer (e.g. one
// full-buffer flush) can span multiple SEND calls that must all land inside
// one START..STOP on the wire. The command/address bytes u8g2's CAD layer
// sends come from short-lived stack locals that get reused by the next call
// almost immediately - only the final pixel-data SendData call points into
// u8g2's own persistent framebuffer - so bytes must be copied out here, at
// SEND time, rather than deferred to END_TRANSFER (deferring would read back
// stale/overwritten stack memory for every command byte, corrupting the
// command stream while leaving the pixel data intact - a state that reliably
// produces "nothing visibly wrong in the data, but the panel shows nothing").
static uint8_t u8x8_byte_esp_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_BYTE_SEND:
        if (s_xfer_len + arg_int > sizeof(s_xfer_buf)) {
            return 0;
        }
        memcpy(&s_xfer_buf[s_xfer_len], arg_ptr, arg_int);
        s_xfer_len += arg_int;
        break;
    case U8X8_MSG_BYTE_INIT:
    case U8X8_MSG_BYTE_SET_DC:
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        s_xfer_len = 0;
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        if (s_xfer_len > 0) {
            esp_err_t err = i2c_master_transmit(s_i2c_dev, s_xfer_buf, s_xfer_len, 1000);
            if (err != ESP_OK) {
                return 0;
            }
        }
        break;
    default:
        return 0;
    }
    return 1;
}

// u8x8 GPIO/delay callback. No reset line or bit-banged D/C pin is wired -
// I2C mode encodes addressing in-band via the CAD layer's control byte - so
// only the delay messages (used during the init command sequence) matter.
static uint8_t u8x8_gpio_and_delay_esp(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:
        vTaskDelay(pdMS_TO_TICKS(arg_int));
        break;
    case U8X8_MSG_DELAY_10MICRO:
        esp_rom_delay_us(arg_int * 10);
        break;
    case U8X8_MSG_DELAY_100NANO:
    case U8X8_MSG_DELAY_NANO:
        break;  // sub-microsecond - not meaningful for a hardware I2C transport
    default:
        break;  // no reset/DC/CS lines wired - nothing to do for GPIO messages
    }
    return 1;
}

esp_err_t oled_init(i2c_master_bus_handle_t bus_handle, u8g2_t *out_u8g2)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_I2C_ADDR,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_i2c_dev);
    if (err != ESP_OK) {
        return err;
    }

    memset(out_u8g2, 0, sizeof(*out_u8g2));
    // Genuinely SH1106, not SSD1306: the panel showed ~2 columns cut off the
    // left with the same content wrapping onto a lit strip on the right -
    // exactly the SH1106 driver's documented x_offset=2 (132-column RAM,
    // only 128 wired to the visible glass, starting 2 columns in), which
    // none of the SSD1306 presets (noname/vcomh0/alt0, all x_offset=0) have.
    u8g2_Setup_sh1106_i2c_128x64_noname_f(out_u8g2, U8G2_R0, u8x8_byte_esp_i2c, u8x8_gpio_and_delay_esp);

    u8g2_InitDisplay(out_u8g2);
    u8g2_SetPowerSave(out_u8g2, 0);

    u8g2_ClearBuffer(out_u8g2);
    u8g2_SetFont(out_u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(out_u8g2, 0, 10, "EngDesDev OLED OK");
    u8g2_SendBuffer(out_u8g2);

    return ESP_OK;
}

void oled_draw_dashboard(u8g2_t *u8g2, float temp_c, float humidity_pct, float adc_v,
                          float spwm_freq_hz, const char *ip_str)
{
    char line[24];

    u8g2_ClearBuffer(u8g2);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);

    if (ip_str[0] != '\0') {
        snprintf(line, sizeof(line), "IP: %s", ip_str);
    } else {
        snprintf(line, sizeof(line), "IP: (no wifi)");
    }
    u8g2_DrawStr(u8g2, 0, 10, line);

    snprintf(line, sizeof(line), "T:%.1fC  H:%.1f%%", temp_c, humidity_pct);
    u8g2_DrawStr(u8g2, 0, 26, line);

    snprintf(line, sizeof(line), "ADC: %.4f V", adc_v);
    u8g2_DrawStr(u8g2, 0, 42, line);

    snprintf(line, sizeof(line), "SPWM: %.1f Hz", spwm_freq_hz);
    u8g2_DrawStr(u8g2, 0, 58, line);

    u8g2_SendBuffer(u8g2);
}
