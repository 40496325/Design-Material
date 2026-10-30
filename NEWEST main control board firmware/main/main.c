// =============================================================================
// EngDes26 — ESP32-S3 Main Crane Controller Firmware
// Fully aligned with MCU Wiring Diagram & Header Pin Allocation (J1 - J9)
// and Interface Control Document (ICD v1) Tables 12, 13, 19, 26, 31, 34, 38
// =============================================================================

#include <stdio.h>
#include <inttypes.h>
#include <string.h>
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
#include "nrf24.h"

#define OLED_ENABLED 0
#define STATUS_LED_GPIO 48
#define PERIPH_PWR_EN_GPIO 47

// I2C Bus (MCP23017 U4, AHT20 U10, J8 Header)
#define I2C_SDA_GPIO 8
#define I2C_SCL_GPIO 9
#define AHT20_I2C_ADDR 0x38

// SPI Bus (ADS1220 ADC, SD Card, nRF24L01+)
#define SPI_HOST_ID   SPI2_HOST
#define SPI_SCK_GPIO  12
#define SPI_MOSI_GPIO 11
#define SPI_MISO_GPIO 13
#define ADC_CS_GPIO   14
#define ADC_DRDY_GPIO 15
#define MMC_CS_GPIO   2
#define ADC_AVDD_NOMINAL_V 3.3f

// MCP23017 Control Pins
#define GPIO_MCP_RESET 5
#define GPIO_MCP_INTB  6
#define GPIO_MCP_INTA  7

// J7 Direct MCU High-Speed PWM & Encoder Pins
#define GPIO_J7_PWM_A   38  // EEII 1 SPWM Phase A (J7.8 / IO1)
#define GPIO_J7_PWM_B   39  // EEII 1 SPWM Phase B (J7.7 / IO2)
#define GPIO_J7_PWM_C   40  // EEII 1 SPWM Phase C (J7.6 / IO3)
#define GPIO_J7_MAG_PWM 41  // EEII 2 Magnet PWM (J7.5 / IO4)
#define GPIO_J7_LAT_PWM 18  // MCTR 1 Lateral Motor PWM ENA (J7.9 / IO_A)
#define GPIO_J7_ENC_A   21  // MCTR 1 Wheel Encoder Ch A (J7.12 / IO_D)
#define GPIO_J7_ENC_B   4   // MCTR 1 Wheel Encoder Ch B (J7.11 / IO_C)

// nRF24L01+ Radio Configuration (J1 + J3.3)
#define NRF_CSN_GPIO    17
#define NRF_SPI_HZ      (4 * 1000 * 1000)
#define NRF_ACK_PAYLOAD 1
#define NRF_CARRIER_TEST 0   // DIAG: set 1 to make the crane radio emit a constant carrier on ch77
// CE is driven identically on BOTH J3.3 (MCP23017 GPA0, final ICD) and J7.10
// (GPIO10, the pin used for the passing 200-packet smoke test; now free per the
// wiring doc). The jumper can sit on either header - this isolates the CE path.
#define NRF_CE_ALT_GPIO  10
// ICD rule (smoke test, 5.5.2): PA_LOW under 2 m - PA_MAX saturates the LNA at close range.
// 0=-18 dBm 1=-12 dBm (PA_LOW) 2=-6 dBm 3=0 dBm (PA_MAX). Use 3 on the rail at range.
#define NRF_PA_LEVEL     1

// MCP23017 Port Allocation Bitmasks
// Port A (Header J3):
//   bit 0: GPA0 (J3.3) = nRF24 CE (Output, driven HIGH at boot for continuous PRX)
//   bit 1: GPA1 (J3.4) = EEII 1 Drive Enable Output (DRV_EN_LOGIC)
//   bit 2: GPA2 (J3.5) = EEII 1 Drive Fault Input (DRV_FAULT_IN)
//   bit 3: GPA3 (J3.6) = EEII 2 Magnet Logic Output (MAG_LOGIC)
#define MCP_PORT_A_IN_MASK   0x04  // GPA2 is input, all others output
#define MCP_PA_NRF_CE        (1 << 0)
#define MCP_PA_DRV_EN        (1 << 1)
#define MCP_PA_DRV_FAULT     (1 << 2)
#define MCP_PA_MAG_LOGIC     (1 << 3)

// Port B (Header J4):
//   bit 0: GPB0 (J4.3) = SD Detect (Input)
//   bit 1: GPB1 (J4.4) = MCTR 1 Motor Direction 1 (MOT_IN1)
//   bit 2: GPB2 (J4.5) = MCTR 1 Motor Direction 2 (MOT_IN2)
//   bit 3: GPB3 (J4.6) = MCTR 1 Optional Direction 3 (IN3)
//   bit 4: GPB4 (J4.7) = MCTR 1 Optional Direction 4 (IN4)
//   bit 5: GPB5 (J4.8) = MCTR 2 Hoist Brake Control (HOIST_BRAKE)
#define MCP_PORT_B_IN_MASK   0xC1  // GPB0, GPB6, GPB7 input, others output
#define MCP_PB_MOT_IN1       (1 << 1)
#define MCP_PB_MOT_IN2       (1 << 2)
#define MCP_PB_MOT_IN3       (1 << 3)
#define MCP_PB_MOT_IN4       (1 << 4)
#define MCP_PB_HOIST_BRAKE   (1 << 5)

// Radio protocol framing constants (ICD Tables 33 & 34)
#define NRF_MAGIC_CMD  0xA5
#define NRF_CMD_PING   0x01
#define NRF_CMD_DOWN   0x02
#define UPLINK_MAGIC   0x81

static const uint8_t NRF_ADDR[5] = { 'R', 'E', 'I', 'I', '2' };
static const char *TAG = "crane_main";

static nrf24_t s_nrf;
static bool s_nrf_ok = false;
static mcp23017_handle_t s_mcp;
static ads1220_handle_t s_ads1220;

static uint8_t s_port_a_val = MCP_PA_NRF_CE | MCP_PA_DRV_EN;
static uint8_t s_port_b_val = 0x00;

// Telemetry state variables (Table 34)
static int16_t  s_trolley_pos_mm = 0;
static int16_t  s_hoist_pos_mm = 0;
static int16_t  s_trolley_vel_mm_s = 0;
static int16_t  s_hoist_vel_mm_s = 0;
static uint16_t s_load_kg_x10 = 0;
static uint16_t s_vfd_current_A_x10 = 0;
static uint8_t  s_magnet_current_A_x10 = 0; // Byte 14 (Table 34)
static uint8_t  s_magnet_status = 0;        // Byte 15
static uint8_t  s_error_code = 0;           // Byte 16
static uint8_t  s_fault_flags = 0;          // Byte 17
static uint8_t  s_system_health = 0x01;     // Byte 18 (bit 0 = Link OK)

static void update_mcp_outputs(void)
{
    mcp23017_write_port(&s_mcp, MCP23017_PORT_A, s_port_a_val);
    mcp23017_write_port(&s_mcp, MCP23017_PORT_B, s_port_b_val);
}

static void build_uplink_frame(uint8_t seq_ack, uint8_t out[NRF24_PAYLOAD_SIZE])
{
    memset(out, 0, NRF24_PAYLOAD_SIZE);
    out[0] = UPLINK_MAGIC;
    out[1] = seq_ack;
    out[2] = (uint8_t)(s_trolley_pos_mm & 0xFF);
    out[3] = (uint8_t)((s_trolley_pos_mm >> 8) & 0xFF);
    out[4] = (uint8_t)(s_hoist_pos_mm & 0xFF);
    out[5] = (uint8_t)((s_hoist_pos_mm >> 8) & 0xFF);
    out[6] = (uint8_t)(s_trolley_vel_mm_s & 0xFF);
    out[7] = (uint8_t)((s_trolley_vel_mm_s >> 8) & 0xFF);
    out[8] = (uint8_t)(s_hoist_vel_mm_s & 0xFF);
    out[9] = (uint8_t)((s_hoist_vel_mm_s >> 8) & 0xFF);
    out[10] = (uint8_t)(s_load_kg_x10 & 0xFF);
    out[11] = (uint8_t)((s_load_kg_x10 >> 8) & 0xFF);
    out[12] = (uint8_t)(s_vfd_current_A_x10 & 0xFF);
    out[13] = (uint8_t)((s_vfd_current_A_x10 >> 8) & 0xFF);
    out[14] = s_magnet_current_A_x10;
    out[15] = s_magnet_status;
    out[16] = s_error_code;
    out[17] = s_fault_flags;
    out[18] = s_system_health;
}

static void process_downlink_cmd(const uint8_t *frame, uint8_t seq)
{
    uint8_t mode_flags = frame[3];
    bool estop  = (mode_flags & 0x01) != 0;
    bool reset  = (mode_flags & 0x02) != 0;
    int8_t trolley = (int8_t)frame[4];
    int8_t hoist   = (int8_t)frame[5];
    uint8_t magnet = frame[6];

    if (estop) {
        // E-STOP: Inhibit VFD drive, clear directions, de-energize magnet, clamp hoist brake
        s_port_a_val &= ~(MCP_PA_DRV_EN | MCP_PA_MAG_LOGIC);
        s_port_b_val &= ~(MCP_PB_MOT_IN1 | MCP_PB_MOT_IN2 | MCP_PB_HOIST_BRAKE);
        gpio_set_level(GPIO_J7_LAT_PWM, 0);
        gpio_set_level(GPIO_J7_MAG_PWM, 0);
        s_error_code = 0x01;
        s_system_health = 0x02; // bit 1 = E-stop
    } else if (reset) {
        s_trolley_pos_mm = 0;
        s_hoist_pos_mm = 0;
        s_trolley_vel_mm_s = 0;
        s_hoist_vel_mm_s = 0;
        s_error_code = 0x00;
        s_system_health = 0x01;
        s_port_a_val |= MCP_PA_DRV_EN;
    } else {
        s_port_a_val |= MCP_PA_DRV_EN;

        // Magnet Logic (J3.6) & PWM (J7.5)
        if (magnet) {
            s_port_a_val |= MCP_PA_MAG_LOGIC;
            gpio_set_level(GPIO_J7_MAG_PWM, 1);
            s_magnet_status = 1; // 1 = ON (Holding)
        } else {
            s_port_a_val &= ~MCP_PA_MAG_LOGIC;
            gpio_set_level(GPIO_J7_MAG_PWM, 0);
            s_magnet_status = 0; // 0 = OFF
        }

        // MCTR 1 Lateral Direction (J4.4 / J4.5) & Speed PWM (J7.9)
        if (trolley > 0) {
            s_port_b_val |= MCP_PB_MOT_IN1;
            s_port_b_val &= ~MCP_PB_MOT_IN2;
            gpio_set_level(GPIO_J7_LAT_PWM, 1);
        } else if (trolley < 0) {
            s_port_b_val &= ~MCP_PB_MOT_IN1;
            s_port_b_val |= MCP_PB_MOT_IN2;
            gpio_set_level(GPIO_J7_LAT_PWM, 1);
        } else {
            s_port_b_val &= ~(MCP_PB_MOT_IN1 | MCP_PB_MOT_IN2);
            gpio_set_level(GPIO_J7_LAT_PWM, 0);
        }

        // MCTR 2 Hoist Holding Brake (J4.8): HIGH = Release (moving), LOW = Clamp (holding)
        if (hoist != 0) {
            s_port_b_val |= MCP_PB_HOIST_BRAKE;
        } else {
            s_port_b_val &= ~MCP_PB_HOIST_BRAKE;
        }

        s_trolley_pos_mm += trolley * 5;
        s_hoist_pos_mm += hoist * 5;
        s_trolley_vel_mm_s = trolley * 20;
        s_hoist_vel_mm_s = hoist * 20;
        s_error_code = 0x00;
        s_system_health = 0x01;
    }

    // Always maintain nRF CE HIGH on J3.3
    s_port_a_val |= MCP_PA_NRF_CE;
    update_mcp_outputs();
}

static void nrf_link_task(void *arg)
{
    (void)arg;
    uint8_t rx_frame[NRF24_PAYLOAD_SIZE];
    uint8_t ack_payload[NRF24_PAYLOAD_SIZE];
    uint32_t rx_count = 0;
    uint32_t rpd_hits = 0;
    TickType_t last_hb = xTaskGetTickCount();

    // Pre-load initial telemetry ACK payload into radio FIFO
    build_uplink_frame(0, ack_payload);
    nrf24_write_ack_payload(&s_nrf, ack_payload);

    while (1) {
        if (nrf24_payload_available(&s_nrf) &&
            nrf24_read_payload(&s_nrf, rx_frame) == ESP_OK) {
            rx_count++;
            uint8_t seq = rx_frame[2];

            if (rx_frame[0] == NRF_MAGIC_CMD && rx_frame[1] == NRF_CMD_PING) {
                ESP_LOGI(TAG, "nrf: PING #%" PRIu32 " from groundstation", rx_count);  // matches tools/capture_evidence.py
            } else if (rx_frame[0] == NRF_MAGIC_CMD && rx_frame[1] == NRF_CMD_DOWN) {
                process_downlink_cmd(rx_frame, seq);
            }

            // Immediately load next telemetry payload for upcoming auto-ACK
            build_uplink_frame(seq, ack_payload);
            nrf24_write_ack_payload(&s_nrf, ack_payload);
        }

        // DIAG: RPD (reg 0x09) = 1 when the radio, in RX mode, saw a carrier > -64 dBm.
        // Sampled every loop so a GS PING a few cm away should register hits.
        {
            uint8_t rpd = 0;
            if (nrf24_read_reg(&s_nrf, 0x09, &rpd) == ESP_OK && (rpd & 0x01)) {
                rpd_hits++;
            }
        }

        if ((xTaskGetTickCount() - last_hb) >= pdMS_TO_TICKS(10000)) {
            last_hb = xTaskGetTickCount();
            uint8_t cfg = 0, st = 0, fifo = 0, ch = 0, rfs = 0, feat = 0, dynpd = 0, gpa = 0;
            nrf24_read_reg(&s_nrf, NRF24_REG_CONFIG, &cfg);
            nrf24_read_reg(&s_nrf, NRF24_REG_STATUS, &st);
            nrf24_read_reg(&s_nrf, NRF24_REG_FIFO_STATUS, &fifo);
            nrf24_read_reg(&s_nrf, NRF24_REG_RF_CH, &ch);
            nrf24_read_reg(&s_nrf, NRF24_REG_RF_SETUP, &rfs);
            nrf24_read_reg(&s_nrf, NRF24_REG_FEATURE, &feat);
            nrf24_read_reg(&s_nrf, NRF24_REG_DYNPD, &dynpd);
            mcp23017_read_port(&s_mcp, MCP23017_PORT_A, &gpa);
            ESP_LOGI(TAG, "nrf: listening - frames=%" PRIu32 " rpd_hits=%" PRIu32, rx_count, rpd_hits);  // matches tools/capture_evidence.py
            ESP_LOGI(TAG, "  CONFIG=0x%02X (want 0x0F) STATUS=0x%02X FIFO=0x%02X RF_CH=%u RF_SETUP=0x%02X "
                          "FEATURE=0x%02X DYNPD=0x%02X | MCP GPIOA=0x%02X (bit0=CE pin level)",
                     cfg, st, fifo, ch, rfs, feat, dynpd, gpa);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void sensor_poll_task(void *arg)
{
    while (1) {
        // Read ADS1220 ADC channels (J6):
        // Channel 0: S-type load cell differential signal (J6.5/J6.6)
        int32_t code_load = 0;
        if (ads1220_read_single_ended(&s_ads1220, 0, &code_load) == ESP_OK) {
            float v = ((float)code_load / 8388608.0f) * ADC_AVDD_NOMINAL_V;
            s_load_kg_x10 = (uint16_t)(v * 100.0f); // 0.1 kg steps
        }

        // Channel 2: EEII 1 VFD Current Sensor (J6.7)
        int32_t code_vfd = 0;
        if (ads1220_read_single_ended(&s_ads1220, 2, &code_vfd) == ESP_OK) {
            float v = ((float)code_vfd / 8388608.0f) * ADC_AVDD_NOMINAL_V;
            s_vfd_current_A_x10 = (uint16_t)(v * 10.0f); // 0.1 A steps
        }

        // Channel 3: EEII 2 Magnet Current Sensor (J6.8)
        int32_t code_mag = 0;
        if (ads1220_read_single_ended(&s_ads1220, 3, &code_mag) == ESP_OK) {
            float v = ((float)code_mag / 8388608.0f) * ADC_AVDD_NOMINAL_V;
            s_magnet_current_A_x10 = (uint8_t)(v * 10.0f); // 0.1 A steps
        }

        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

static void init_peripheral_power(void)
{
    gpio_config_t pwr_cfg = {
        .pin_bit_mask = 1ULL << PERIPH_PWR_EN_GPIO,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&pwr_cfg));
    gpio_set_level(PERIPH_PWR_EN_GPIO, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
}

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

void app_main(void)
{
    esp_err_t nvs_err = nvs_flash_init();
    if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        nvs_err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(nvs_err);

    init_peripheral_power();

    // Reset MCP23017 U4
    gpio_config_t mcp_reset = {
        .pin_bit_mask = 1ULL << GPIO_MCP_RESET,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&mcp_reset));
    gpio_set_level(GPIO_MCP_RESET, 1);

    // Direct GPIOs on J7 (PWM)
    gpio_config_t j7_cfg = {
        .pin_bit_mask = (1ULL << GPIO_J7_LAT_PWM) | (1ULL << GPIO_J7_MAG_PWM),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&j7_cfg));
    gpio_set_level(GPIO_J7_LAT_PWM, 0);
    gpio_set_level(GPIO_J7_MAG_PWM, 0);

    i2c_master_bus_handle_t i2c_bus = init_i2c_bus();

    // Initialize MCP23017 and configure port directions
    if (mcp23017_init(i2c_bus, MCP23017_I2C_ADDR_DEFAULT, &s_mcp) == ESP_OK &&
        mcp23017_probe(&s_mcp) == ESP_OK) {
        mcp23017_set_direction(&s_mcp, MCP23017_PORT_A, MCP_PORT_A_IN_MASK);
        mcp23017_set_direction(&s_mcp, MCP23017_PORT_B, MCP_PORT_B_IN_MASK);
        // Hold J3.3 (nRF24 CE) LOW while the radio is powered up and configured.
        // nRF24L01+ only enters RX mode from Standby-I on CE rising with PRIM_RX=1
        // (datasheet state diagram). If CE is already HIGH when PWR_UP is set with
        // PRIM_RX=0, the radio parks in Standby-II and never starts listening.
        // CE is raised after nrf24_start_listening() below.
        s_port_a_val = MCP_PA_DRV_EN;
        s_port_b_val = 0x00;
        update_mcp_outputs();
        ESP_LOGI(TAG, "MCP23017 initialized. J3.3 (nRF CE) held LOW for radio config.");
    } else {
        ESP_LOGE(TAG, "MCP23017 initialization failed!");
    }

    init_spi_bus();

    // Initialize nRF24 with CE controlled via MCP23017 (GPIO_NUM_NC)
    spi_device_interface_config_t dev_cfg = {
        .mode = 0,
        .clock_speed_hz = NRF_SPI_HZ,
        .spics_io_num = NRF_CSN_GPIO,
        .queue_size = 4,
    };
    spi_device_handle_t spi_nrf;
    if (spi_bus_add_device(SPI_HOST_ID, &dev_cfg, &spi_nrf) == ESP_OK &&
        nrf24_init(&s_nrf, spi_nrf, (gpio_num_t)NRF_CE_ALT_GPIO) == ESP_OK &&
        nrf24_probe(&s_nrf) == ESP_OK) {
        const nrf24_config_t cfg = {
            .channel = 77,
            .rate_2mbps = true,
            .pa_dbm = NRF_PA_LEVEL,
            .arc = 5,
            .ard_steps = 1,
            .ack_payload = true,
            .addr5 = NRF_ADDR,
        };
        if (nrf24_configure(&s_nrf, &cfg) == ESP_OK &&
            nrf24_start_listening(&s_nrf) == ESP_OK) {
            // PRIM_RX|PWR_UP now set: CE rising edge (J3.3) enters RX mode.
            s_port_a_val |= MCP_PA_NRF_CE;
            update_mcp_outputs();
            vTaskDelay(pdMS_TO_TICKS(1));   // >130 us RX settling
            ESP_LOGI(TAG, "CE HIGH on J3.3 (GPA0) and J7.10 (GPIO%d) -> RX mode", NRF_CE_ALT_GPIO);
            s_nrf_ok = true;
            ESP_LOGI(TAG, "nRF24 crane link active on channel 77 @ 2 Mbps (PRX on 'REII2')");
        }
    }

#if NRF_CARRIER_TEST
    // DIAG: turn the crane radio into a continuous carrier on ch77 (datasheet
    // App. C: PWR_UP=1, PRIM_RX=0, CONT_WAVE=1, PLL_LOCK=1, CE=1). Run "RPD 2000"
    // on the groundstation: hits near samples = crane TX + GS RX both work.
    if (s_nrf_ok) {
        s_port_a_val &= ~MCP_PA_NRF_CE;                 // CE low -> Standby-I
        update_mcp_outputs();
        gpio_set_level((gpio_num_t)NRF_CE_ALT_GPIO, 0);
        nrf24_write_reg(&s_nrf, NRF24_REG_CONFIG,
                        NRF24_CFG_PWR_UP | NRF24_CFG_EN_CRC | NRF24_CFG_CRCO);  // PRIM_RX=0
        nrf24_write_reg(&s_nrf, NRF24_REG_RF_SETUP, 0x0E | 0x80 | 0x10);        // 2M, 0 dBm, CONT_WAVE, PLL_LOCK
        s_port_a_val |= MCP_PA_NRF_CE;                  // CE high -> carrier on
        update_mcp_outputs();
        gpio_set_level((gpio_num_t)NRF_CE_ALT_GPIO, 1);
        ESP_LOGW(TAG, "NRF_CARRIER_TEST=1: crane radio transmitting constant carrier on ch77 (link task disabled)");
        s_nrf_ok = false;                               // don't start the PRX link task
    }
#endif

    if (ads1220_init(SPI_HOST_ID, ADC_CS_GPIO, ADC_DRDY_GPIO, &s_ads1220) == ESP_OK) {
        ads1220_probe(&s_ads1220);
        xTaskCreate(sensor_poll_task, "sensor_poll", 4096, NULL, tskIDLE_PRIORITY + 1, NULL);
    }

    sdcard_handle_t sdcard;
    sdcard_init(SPI_HOST_ID, MMC_CS_GPIO, &sdcard);
    spwm_init(GPIO_J7_PWM_A, GPIO_J7_PWM_B, GPIO_J7_PWM_C, 50.0f);
    status_led_start(STATUS_LED_GPIO);

    if (s_nrf_ok) {
        xTaskCreate(nrf_link_task, "nrf_link", 4096, NULL, tskIDLE_PRIORITY + 2, NULL);
    }
}