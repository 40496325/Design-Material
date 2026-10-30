// =============================================================================
// rig_console.c - typed commands on the crane board's USB port (COM12)
// =============================================================================
// COM12 is the ESP32-S3's built-in USB Serial/JTAG port (USB-C on the board).
// sdkconfig.defaults makes it the PRIMARY console, so it carries input as well
// as log output (as a "secondary" console it is output-only, which is why
// typing into the monitor did nothing before).
//
// Lines are handed to rf_link.c, which executes them inside the radio task.
// =============================================================================
#include "rig_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "rf_link.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#elif CONFIG_ESP_CONSOLE_UART
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#endif

static const char *TAG = "console";

static int console_read(uint8_t *buf, int len)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    return usb_serial_jtag_read_bytes(buf, len, pdMS_TO_TICKS(20));
#elif CONFIG_ESP_CONSOLE_UART
    return uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM, buf, len, pdMS_TO_TICKS(20));
#else
    (void)buf;
    (void)len;
    vTaskDelay(pdMS_TO_TICKS(1000));
    return 0;
#endif
}

static void console_task(void *arg)
{
    (void)arg;
    char line[96];
    size_t pos = 0;
    uint8_t chunk[64];

    for (;;) {
        int n = console_read(chunk, sizeof(chunk));
        for (int i = 0; i < n; i++) {
            char c = (char)chunk[i];
            if (c == '\r' || c == '\n') {
                if (pos > 0) {
                    line[pos] = '\0';
                    rf_link_submit_line(line);
                    pos = 0;
                }
            } else if (c == 0x08 || c == 0x7F) {   // backspace from a terminal
                if (pos > 0) pos--;
            } else if (pos < sizeof(line) - 1 && c >= 0x20) {
                line[pos++] = c;
            }
        }
    }
}

void rig_console_start(void)
{
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    usb_serial_jtag_driver_config_t cfg = {
        .rx_buffer_size = 1024,
        .tx_buffer_size = 4096,   // room for a full SCAN dump without blocking the radio task
    };
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "USB Serial/JTAG driver install failed - console input disabled");
        return;
    }
    usb_serial_jtag_vfs_use_driver();
#elif CONFIG_ESP_CONSOLE_UART
    if (uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 1024, 4096, 0, NULL, 0) != ESP_OK) {
        ESP_LOGE(TAG, "UART driver install failed - console input disabled");
        return;
    }
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
#else
    ESP_LOGW(TAG, "no console configured - typed commands disabled");
#endif
    setvbuf(stdout, NULL, _IOLBF, 0);   // whole lines, so RIG,... records never interleave mid-line
    xTaskCreate(console_task, "console", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    ESP_LOGI(TAG, "typed commands enabled on the console port - type HELP");
}
