idf_component_register(SRCS "main.c" "status_led.c" "mcp23017.c" "ads1220.c" "sdcard.c" "spwm.c" "oled.c"
                            "nrf24.c" "rf_link.c" "rig_console.c" "rig_telemetry.c" "loadcell.c"
                            "pintest.c"   # v5.0 PINTEST (Table 43 stage 4 handshake)
                            # Drive electronics (EEII 1 VFD, ICD IF6.0), added v4.5.0 - kept in its own folder
                            "drive_electronics/mcp_shared.c" "drive_electronics/drive_spwm.c"
                            "drive_electronics/drive_control.c"
                            # Trolley (MCTR 1 lateral, ICD IF1.0), added v4.6.0 - kept in its own folder
                            "trolley/trolley_io.c" "trolley/trolley_control.c"
                            # Magnet (EEII 2 load attachment, ICD IF12.0), added v4.7.0 - kept in its own folder
                            "magnet/magnet_control.c"
                        INCLUDE_DIRS "." "drive_electronics" "trolley" "magnet"
                        # NOTE: wifi_app.c, web_server.c and the embedded index.html dashboard page
                        # have been removed from this starter project entirely (see main.c for why).
                        # oled.c is still built in - see OLED_ENABLED in main.c to turn it on.
                        # REII (Increment 4): nrf24.c = radio register driver, rf_link.c = crane
                        # link task (ICD IF10.0), rig_console.c = typed commands on COM12.
                        PRIV_REQUIRES esp_driver_gpio esp_driver_i2c esp_driver_spi esp_driver_sdspi sdmmc esp_system spi_flash esp_timer
                                       nvs_flash esp_driver_mcpwm esp_driver_ledc esp_driver_pcnt u8g2
                                       esp_driver_usb_serial_jtag esp_driver_uart esp_rom)
