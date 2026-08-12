idf_component_register(SRCS "update_OTA.c"
                    INCLUDE_DIRS "inc"
                    PRIV_REQUIRES esp_wifi nvs_flash esp-tls esp_https_ota esp_driver_gpio
                    REQUIRES app_update esp_event esp_http_client mbedtls camera espressif__esp32-camera)
