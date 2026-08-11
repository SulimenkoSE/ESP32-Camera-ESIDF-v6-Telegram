idf_component_register(SRCS "telegram.c"
                    INCLUDE_DIRS "inc"
                    PRIV_REQUIRES esp_wifi nvs_flash esp-tls esp_driver_gpio espressif__cjson espressif__esp32-camera
                    REQUIRES esp_event esp_http_client mbedtls camera "main")
