#include "status_led.h"
#include "led_strip.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <math.h>

#define BREATH_PERIOD_MS   1200
#define BREATH_STEP_MS      20
#define BREATH_MAX_LEVEL     60  // keep it dim, this thing sits right next to your eyes

#define BLINK_ON_MS   100
#define BLINK_OFF_MS  100

typedef enum {
    LED_EVENT_TX,  // green blink
    LED_EVENT_RX,  // orange blink
} led_event_t;

// A FreeRTOS queue is a thread-safe, fixed-size FIFO buffer - the standard
// way to hand small pieces of data from one task to another without them
// touching shared memory directly. Here, whichever task calls
// status_led_notify_tx()/_rx() (from any other file) just drops an event
// into the queue via xQueueSend(); status_led_task below is the only thing
// that ever reads from it (xQueueReceive()), so there's no race to worry
// about on the receiving end.
static QueueHandle_t s_event_queue;
static volatile bool s_rx_mode = false;

static void set_color(led_strip_handle_t strip, uint8_t red, uint8_t green, uint8_t blue) {
    led_strip_set_pixel(strip, 0, red, green, blue);
    led_strip_refresh(strip);
}

static void play_blink(led_strip_handle_t strip, led_event_t evt) {
    uint8_t red = (evt == LED_EVENT_RX) ? BREATH_MAX_LEVEL : 0;
    uint8_t green = (evt == LED_EVENT_TX) ? BREATH_MAX_LEVEL : (evt == LED_EVENT_RX ? BREATH_MAX_LEVEL / 3 : 0);

    for (int i = 0; i < 2; i++) {
        set_color(strip, red, green, 0);
        vTaskDelay(pdMS_TO_TICKS(BLINK_ON_MS));
        set_color(strip, 0, 0, 0);
        vTaskDelay(pdMS_TO_TICKS(BLINK_OFF_MS));
    }
}

static void status_led_task(void *arg) {
    led_strip_handle_t strip = (led_strip_handle_t)arg;
    float phase = 0.0f;
    const float phase_step = (2.0f * (float)M_PI * BREATH_STEP_MS) / BREATH_PERIOD_MS;

    while (1) {
        led_event_t evt;
        if (xQueueReceive(s_event_queue, &evt, 0) == pdTRUE) {
            play_blink(strip, evt);
            continue;
        }

        float level = (sinf(phase) + 1.0f) * 0.5f; // 0..1
        uint8_t brightness = (uint8_t)(level * BREATH_MAX_LEVEL);

        if (s_rx_mode) {
            set_color(strip, brightness, brightness / 3, 0); // orange
        } else {
            set_color(strip, 0, brightness, 0); // green
        }

        phase += phase_step;
        if (phase > 2.0f * (float)M_PI) {
            phase -= 2.0f * (float)M_PI;
        }

        vTaskDelay(pdMS_TO_TICKS(BREATH_STEP_MS));
    }
}

void status_led_start(int gpio_num) {
    led_strip_handle_t strip;

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    led_strip_rmt_config_t rmt_config = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };

    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &strip));
    led_strip_clear(strip);

    s_event_queue = xQueueCreate(2, sizeof(led_event_t));

    // Small, fixed-purpose task - 2KB stack is comfortably enough here.
    xTaskCreate(status_led_task, "status_led", 2048, strip, tskIDLE_PRIORITY + 1, NULL);
}

void status_led_set_rx_mode(bool rx_active) {
    s_rx_mode = rx_active;
}

void status_led_notify_tx(void) {
    led_event_t evt = LED_EVENT_TX;
    xQueueSend(s_event_queue, &evt, 0); // non-blocking, drop if a blink is already queued
}

void status_led_notify_rx(void) {
    led_event_t evt = LED_EVENT_RX;
    xQueueSend(s_event_queue, &evt, 0);
}
