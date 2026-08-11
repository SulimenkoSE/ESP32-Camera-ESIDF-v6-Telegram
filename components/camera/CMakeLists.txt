idf_component_register(SRCS "camera_my.c"
                    INCLUDE_DIRS "inc"
                    PRIV_REQUIRES esp_wifi nvs_flash log esp-tls esp_driver_gpio espressif__esp32-camera
                    REQUIRES esp_http_client mbedtls telegram)
