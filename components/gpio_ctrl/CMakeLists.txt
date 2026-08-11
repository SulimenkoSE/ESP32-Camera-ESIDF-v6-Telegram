idf_component_register(SRCS "gpio_ctrl.c"
                       INCLUDE_DIRS "inc"
                       PRIV_REQUIRES esp_driver_gpio esp_timer freertos espressif__esp32-camera
                       REQUIRES log telegram)
