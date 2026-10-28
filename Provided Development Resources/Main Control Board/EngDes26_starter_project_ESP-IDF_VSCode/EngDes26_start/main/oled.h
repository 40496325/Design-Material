// u8g2 is a popular open-source graphics library that handles font
// rendering and framebuffer management for a huge range of small
// monochrome displays (including the SSD1306/SH1106 this driver targets) -
// see components/u8g2 for its (vendored, unmodified) source. This module
// is just the glue: the two callbacks u8g2 needs to actually talk I2C to
// this specific board, plus one convenience "draw all the live values"
// function.
//
// NOTE: this OLED support is compiled in but disabled by default in this
// starter project - see OLED_ENABLED near the top of main.c. Most students
// won't have this display wired up; if you do, flip that flag on.

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"
#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Brings up the SSD1306 OLED (I2C, address 0x3C, 128x64, full
 *        framebuffer) via u8g2 on the given bus, clears it, and draws a
 *        confirmation string. On success, out_u8g2 is ready for further
 *        u8g2_* drawing calls (remember to call u8g2_SendBuffer() after
 *        drawing to flush the framebuffer to the panel).
 */
esp_err_t oled_init(i2c_master_bus_handle_t bus_handle, u8g2_t *out_u8g2);

/**
 * @brief Redraws the dashboard: temperature, humidity, ADC AIN0 voltage,
 *        SPWM setpoint, and (if you have WiFi wired up yourself) an IP
 *        address line.
 *
 * @param ip_str Pass "" if there's no IP address to show (e.g. no WiFi in
 *               this starter project) - shown on screen as "(no wifi)".
 */
void oled_draw_dashboard(u8g2_t *u8g2, float temp_c, float humidity_pct, float adc_v,
                          float spwm_freq_hz, const char *ip_str);

#ifdef __cplusplus
}
#endif
