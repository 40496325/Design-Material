// mcp_shared.c - see mcp_shared.h
#include "mcp_shared.h"

#include "driver/i2c_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define REG_IODIR(p) ((uint8_t)(0x00 + (p)))
#define REG_GPPU(p)  ((uint8_t)(0x0C + (p)))
#define REG_GPIO(p)  ((uint8_t)(0x12 + (p)))
#define REG_OLAT(p)  ((uint8_t)(0x14 + (p)))

static mcp23017_handle_t *s_mcp;
static SemaphoreHandle_t s_lock;
static StaticSemaphore_t s_lock_buf;
static uint8_t s_iodir[2] = {0xFF, 0xFF};
static uint8_t s_gppu[2] = {0x00, 0x00};
static uint8_t s_olat[2] = {0x00, 0x00};

static esp_err_t wr(uint8_t reg, uint8_t value)
{
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_mcp->dev, buf, sizeof(buf), pdMS_TO_TICKS(50));
}

static esp_err_t rd(uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive(s_mcp->dev, &reg, 1, value, 1, pdMS_TO_TICKS(50));
}

static bool valid(mcp23017_port_t port, int bit)
{
    return s_mcp && (port == MCP23017_PORT_A || port == MCP23017_PORT_B) && bit >= 0 && bit <= 7;
}

esp_err_t mcp_shared_init(mcp23017_handle_t *mcp)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutexStatic(&s_lock_buf);
    s_mcp = mcp;
    s_iodir[0] = s_iodir[1] = 0xFF;   // power-on reset state of the MCP23017
    s_gppu[0] = s_gppu[1] = 0x00;
    s_olat[0] = s_olat[1] = 0x00;
    return mcp ? ESP_OK : ESP_ERR_INVALID_STATE;
}

bool mcp_shared_ok(void) { return s_mcp != NULL; }

esp_err_t mcp_shared_config_output(mcp23017_port_t port, int bit, int initial_level)
{
    if (!valid(port, bit)) return ESP_ERR_INVALID_STATE;
    const uint8_t m = (uint8_t)(1u << bit);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_olat[port] = initial_level ? (uint8_t)(s_olat[port] | m) : (uint8_t)(s_olat[port] & ~m);
    s_iodir[port] = (uint8_t)(s_iodir[port] & ~m);
    s_gppu[port] = (uint8_t)(s_gppu[port] & ~m);
    esp_err_t err = wr(REG_OLAT(port), s_olat[port]);            // level first ...
    if (err == ESP_OK) err = wr(REG_IODIR(port), s_iodir[port]);  // ... then direction
    if (err == ESP_OK) err = wr(REG_GPPU(port), s_gppu[port]);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t mcp_shared_config_input(mcp23017_port_t port, int bit, bool pullup)
{
    if (!valid(port, bit)) return ESP_ERR_INVALID_STATE;
    const uint8_t m = (uint8_t)(1u << bit);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_iodir[port] = (uint8_t)(s_iodir[port] | m);
    s_gppu[port] = pullup ? (uint8_t)(s_gppu[port] | m) : (uint8_t)(s_gppu[port] & ~m);
    esp_err_t err = wr(REG_GPPU(port), s_gppu[port]);
    if (err == ESP_OK) err = wr(REG_IODIR(port), s_iodir[port]);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t mcp_shared_write(mcp23017_port_t port, int bit, int level)
{
    if (!valid(port, bit)) return ESP_ERR_INVALID_STATE;
    const uint8_t m = (uint8_t)(1u << bit);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_olat[port] = level ? (uint8_t)(s_olat[port] | m) : (uint8_t)(s_olat[port] & ~m);
    esp_err_t err = wr(REG_OLAT(port), s_olat[port]);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t mcp_shared_read_port(mcp23017_port_t port, uint8_t *out_levels)
{
    if (!valid(port, 0)) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = rd(REG_GPIO(port), out_levels);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t mcp_shared_reapply(void)
{
    if (!s_mcp) return ESP_ERR_INVALID_STATE;
    esp_err_t err = ESP_OK;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int p = 0; p < 2; p++) {
        esp_err_t e = wr(REG_OLAT(p), s_olat[p]);
        if (e == ESP_OK) e = wr(REG_GPPU(p), s_gppu[p]);
        if (e == ESP_OK) e = wr(REG_IODIR(p), s_iodir[p]);
        if (e != ESP_OK) err = e;
    }
    xSemaphoreGive(s_lock);
    return err;
}

uint8_t mcp_shared_olat(mcp23017_port_t port) { return s_olat[port & 1]; }
uint8_t mcp_shared_iodir(mcp23017_port_t port) { return s_iodir[port & 1]; }
