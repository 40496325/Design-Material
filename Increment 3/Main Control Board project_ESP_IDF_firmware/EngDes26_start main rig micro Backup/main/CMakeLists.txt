idf_component_register(SRCS "main.c" "status_led.c" "mcp23017.c" "ads1220.c" "sdcard.c" "spwm.c" "oled.c" "nrf24.c"
                        INCLUDE_DIRS "."
                        # NOTE: wifi_app.c, web_server.c and the embedded index.html dashboard page
                        # have been removed from this starter project entirely (see main.c for why).
                        # oled.c is still built in - see OLED_ENABLED in main.c to turn it on.
                        # nrf24.c = minimal register-level nRF24L01+ driver for the REII link (docs/27).
                        PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi sdmmc esp_system spi_flash esp_timer esp_rom
                                       nvs_flash esp_driver_mcpwm u8g2)
