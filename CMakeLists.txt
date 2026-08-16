# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Додаємо глобальний макрос (можна просто назву, або зі значенням Camera=1)
add_compile_definitions(Camera)

# "Trim" the build. Include the minimal set of components, main, and anything it depends on.
idf_build_set_property(MINIMAL_BUILD ON)
project(station)

# ============================================================================
# АВТОМАТИЧНЕ ШИФРУВАННЯ OTA БІНАРНИКА ДЛЯ GITHUB (ESP-IDF v6 RSA)
# ============================================================================

# Створюємо нову ізольовану ціль для шифрування
add_custom_target(encrypt_ota_binary ALL
    # Викликаємо спеціалізований скрипт від компонента esp_encrypted_img
    COMMAND python "${CMAKE_SOURCE_DIR}/managed_components/espressif__esp_encrypted_img/tools/esp_enc_img_gen.py"
            encrypt
            "${CMAKE_BINARY_DIR}/station.bin"
            "${CMAKE_SOURCE_DIR}/main/ota_public_key.pem"
            "${CMAKE_SOURCE_DIR}/station_enc.bin"
    COMMENT "Автоматичне шифрування OTA бінарника за допомогою утиліти esp_enc_img_gen..."
    VERBATIM
)

# Кажемо CMake почекати, поки ESP-IDF повністю збере бінарник "app"
add_dependencies(encrypt_ota_binary app)