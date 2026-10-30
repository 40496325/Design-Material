// Drives the single onboard WS2812 ("NeoPixel") RGB LED as a general status
// indicator: a slow green "breathing" (fade in/out) animation at idle, with
// short blink overlays you can trigger from anywhere in your code.
//
// NOTE: the tx/rx naming below is carried over from a sibling project that
// had a LoRa radio on this LED - this board doesn't have one, and nothing
// in this starter's main.c calls status_led_notify_tx/rx or
// status_led_set_rx_mode. They're left in as a ready-made "flash the LED
// when X happens" hook for your own code (e.g. call status_led_notify_tx()
// whenever your project sends something, whatever "sends" means for you).

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Starts the breathing status LED task on the given GPIO. Call once at boot. */
void status_led_start(int gpio_num);

/**
 * @brief Switch the breathing baseline color: green (false, general status/idle)
 *        or orange (true, actively listening/receiving).
 */
void status_led_set_rx_mode(bool rx_active);

/** @brief Overlay a brief green blink on the breathing animation - call once per "transmit" event. */
void status_led_notify_tx(void);

/** @brief Overlay a brief orange blink on the breathing animation - call once per "receive" event. */
void status_led_notify_rx(void);

#ifdef __cplusplus
}
#endif
