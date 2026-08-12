idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_gpio espressif__cjson nvs_flash log esp-tls esp32-camera
                    INCLUDE_DIRS "inc"
                    REQUIRES esp_event esp_http_client telegram sntp_time camera gpio_ctrl update_OTA)
