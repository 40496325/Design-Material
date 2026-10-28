#include "mcp23017.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Register addresses, from the datasheet's register map (Table 1-3):
//   IODIRx - direction, 1=input/0=output per bit (reset default: 0xFF, all inputs)
//   GPIOx  - reads the actual pin level (works for both input and output pins)
//   OLATx  - the output latch: what this chip is currently driving on pins
//            configured as outputs (writing GPIOx also works and is more
//            common, but OLATx is what write_reg() below targets since it
//            reads back exactly what was last written, even for a pin that's
//            momentarily being pulled the other way externally)
#define MCP23017_REG_IODIRA 0x00
#define MCP23017_REG_IODIRB 0x01
#define MCP23017_REG_GPIOA  0x12
#define MCP23017_REG_GPIOB  0x13
#define MCP23017_REG_OLATA  0x14
#define MCP23017_REG_OLATB  0x15

#define PROBE_RETRY_COUNT     5
#define PROBE_RETRY_DELAY_MS 20

static esp_err_t write_reg(mcp23017_handle_t *handle, uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(handle->dev, buf, sizeof(buf), pdMS_TO_TICKS(100));
}

static esp_err_t read_reg(mcp23017_handle_t *handle, uint8_t reg, uint8_t *out_value)
{
    return i2c_master_transmit_receive(handle->dev, &reg, 1, out_value, 1, pdMS_TO_TICKS(100));
}

esp_err_t mcp23017_init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr, mcp23017_handle_t *out_handle)
{
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = i2c_addr,
        .scl_speed_hz = 400000,
    };
    return i2c_master_bus_add_device(bus_handle, &dev_cfg, &out_handle->dev);
}

esp_err_t mcp23017_probe(mcp23017_handle_t *handle)
{
    esp_err_t err = ESP_FAIL;

    // Same peripheral-rail power-on race seen on every other device on this
    // bus - a same-boot retry clears it up without any wiring changes.
    for (int attempt = 1; attempt <= PROBE_RETRY_COUNT; attempt++) {
        uint8_t iodira, iodirb;

        err = read_reg(handle, MCP23017_REG_IODIRA, &iodira);
        if (err == ESP_OK) {
            err = read_reg(handle, MCP23017_REG_IODIRB, &iodirb);
        }

        if (err == ESP_OK) {
            if (iodira == 0xFF && iodirb == 0xFF) {
                return ESP_OK;
            }
            // Readable, but not showing the documented post-reset default -
            // either something already reconfigured it, or this isn't
            // really an MCP23017.
            err = ESP_ERR_INVALID_RESPONSE;
        }

        vTaskDelay(pdMS_TO_TICKS(PROBE_RETRY_DELAY_MS));
    }

    return err;
}

esp_err_t mcp23017_set_direction(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t input_mask)
{
    uint8_t reg = (port == MCP23017_PORT_A) ? MCP23017_REG_IODIRA : MCP23017_REG_IODIRB;
    return write_reg(handle, reg, input_mask);
}

esp_err_t mcp23017_write_port(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t value)
{
    uint8_t reg = (port == MCP23017_PORT_A) ? MCP23017_REG_OLATA : MCP23017_REG_OLATB;
    return write_reg(handle, reg, value);
}

esp_err_t mcp23017_read_port(mcp23017_handle_t *handle, mcp23017_port_t port, uint8_t *out_value)
{
    uint8_t reg = (port == MCP23017_PORT_A) ? MCP23017_REG_GPIOA : MCP23017_REG_GPIOB;
    return read_reg(handle, reg, out_value);
}
