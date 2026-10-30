// Driver for the MCP23017, an I2C 16-bit GPIO expander - see
// Datasheets/MCP23017.pdf. In short: it gives you 16 extra GPIO pins
// (split into two 8-bit "ports", A and B) that you control over the I2C
// bus instead of wiring them directly to the ESP32-S3. Handy whenever
// you've run out of GPIOs on the microcontroller itself, or want to
// isolate a bank of pins behind a simple bus instead of a bundle of wires.
//
// Each port is just one byte: bit N of a port corresponds to physical pin
// N on that port (GPA0-GPA7 for port A, GPB0-GPB7 for port B). Whether a
// given bit is an input or output is set once via mcp23017_set_direction()
// (this mirrors a real GPIO's "pinMode" step - you must set a pin's
// direction before reading/writing it meaningfully).

#pragma once

#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MCP23017_I2C_ADDR_DEFAULT 0x20

typedef struct {
    i2c_master_dev_handle_t dev;
} mcp23017_handle_t;

typedef enum {
    MCP23017_PORT_A = 0,  // GPA0-GPA7
    MCP23017_PORT_B = 1,  // GPB0-GPB7
} mcp23017_port_t;

/** @brief Attaches the MCP23017 as an I2C device on the given bus. */
esp_err_t mcp23017_init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr, mcp23017_handle_t *out_handle);

/**
 * @brief Confirms the chip is present and its registers read back sane values.
 *
 * Reads IODIRA/IODIRB, which both reset to 0xFF (all pins inputs). Matching
 * that post-reset default is a real functional check - not just an address
 * ACK - as long as nothing has reconfigured the chip yet.
 */
esp_err_t mcp23017_probe(mcp23017_handle_t *handle);

/** @brief Sets a port's pin directions: 1 = input, 0 = output (per-bit). */
esp_err_t mcp23017_set_direction(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t input_mask);

/** @brief Writes a port's output latch (affects pins currently set as outputs). */
esp_err_t mcp23017_write_port(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t value);

/** @brief Reads a port's current pin levels. */
esp_err_t mcp23017_read_port(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t *out_value);

#ifdef __cplusplus
}
#endif
