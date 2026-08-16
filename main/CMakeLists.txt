idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES esp_wifi nvs_flash esp_driver_gpio espressif__cjson nvs_flash log esp-tls esp32-camera
                    INCLUDE_DIRS "inc"
                    REQUIRES esp_event esp_http_client telegram sntp_time camera gpio_ctrl update_OTA esp_psram
                    REQUIRES esp_encrypted_img)
# Після реєстрації вбудовуємо наш файл ключа
# Параметр BINARY вказує, що файл є двоїчним (не текстовим) якщо bin файл тоді
# target_add_binary_data(${COMPONENT_TARGET} "ota_encryption_key.bin" BINARY)
# якщо KEY тоді
# Вбудовуємо приватний RSA ключ в плату
target_add_binary_data(${COMPONENT_TARGET} "ota_private_key.pem" TEXT)
