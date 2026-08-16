#include <stdio.h>
#include "update_OTA.h"

#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_encrypted_img.h" //Шифрування
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_camera.h" // Обов'язково для роботи з функціями камери

#include "gpio_ctrl.h"

static const char *TAG = "GITHUB_OTA";

// Поточна версія прошивки у вашому коді
#define CURRENT_VERSION "1.0.1"

// Посилання на GitHub (замініть ВАШ_НІК та ІМ'Я_РЕПОЗИТОРІЮ)
#define GITHUB_VERSION_URL "https://raw.githubusercontent.com/SulimenkoSE/ESP32-Camera-ESIDF-v6-Telegram/main/version.txt"
#define GITHUB_BIN_URL     "https://raw.githubusercontent.com/SulimenkoSE/ESP32-Camera-ESIDF-v6-Telegram/main/station_enc.bin"

#define VERSION_BUF_SIZE 32

// Системні вказівники на початок і кінець вбудованого файлу ключа.
// Назва генерується автоматично за принципом: _binary_[ім'я_файлу]_[розширення]_start
// Назви мають ТОЧНО відповідати структурі: _binary_[назва_файлу]_[розширення]_start
// Для bin файлу
//extern const uint8_t ota_encryption_key_bin_start[] asm("_binary_ota_encryption_key_bin_start");
//extern const uint8_t ota_encryption_key_bin_end[]   asm("_binary_ota_encryption_key_bin_end");
// Для реm файлу оголошуємо нові вказівники на RSA-ключ
extern const uint8_t ota_private_key_pem_start[] asm("_binary_ota_private_key_pem_start");
extern const uint8_t ota_private_key_pem_end[]   asm("_binary_ota_private_key_pem_end");

extern volatile bool telegram_bot_paused;

// Кастомний колбек розшифрування для esp_https_ota
static esp_err_t my_ota_decrypt_cb(decrypt_cb_arg_t *args, void *user_ctx)
{
    // Перевірка надійності вхідних даних
    if (args == NULL || user_ctx == NULL || args->data_in == NULL || args->data_in_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Створюємо структуру аргументів, яку очікує компонент esp_encrypted_img
    pre_enc_decrypt_arg_t img_args = {
        .data_in = args->data_in,
        .data_in_len = args->data_in_len
    };

    // Приведення контексту до хендлу дешифратора
    esp_decrypt_handle_t decrypt_handle = (esp_decrypt_handle_t)user_ctx;
    
    // Виклик офіційної функції дешифрування бібліотеки
    esp_err_t err = esp_encrypted_img_decrypt_data(decrypt_handle, &img_args);

    if (err != ESP_OK && err != ESP_ERR_NOT_FINISHED) {
        ESP_LOGE("OTA_DECRYPT", "Збій криптографічного розшифрування: %s", esp_err_to_name(err));
        return err;
    }

    // Записуємо результати назад у структуру args для esp_https_ota
    args->data_out = img_args.data_out;
    args->data_out_len = img_args.data_out_len;

    return ESP_OK;
}

// Функція безпосереднього завантаження бінарного файлу
static void perform_https_ota(void)
{   // Впроваджуємо перевірку ключа перед початком будь-яких дій
    size_t rsa_key_len = ota_private_key_pem_end - ota_private_key_pem_start;
    // Безпековий запобіжник для pem ФАЙЛУ
    if (rsa_key_len < 2000) { 
        ESP_LOGE(TAG, "КРИТИЧНА ПОМИЛКА: RSA-ключ відсутній або пошкоджений! Розмір: %d байт. OTA скасовано.", rsa_key_len);
        return; 
    }

    ESP_LOGI(TAG, "Критично: Звільняємо пам'ять DRAM/IRAM перед OTA...");
    ESP_LOGI(TAG, "Деініціалізація камери для вивільнення фрейм-буферів...");
    esp_err_t cam_err = esp_camera_deinit();
    if (cam_err != ESP_OK) {
        ESP_LOGW(TAG, "Камера не була запущена або помилка зупинки: %s", esp_err_to_name(cam_err));
    }

    // Тут за потреби знімаємо обробники переривань:
    stop_gpio_interrupt(INPUT_SIGNAL_GPIO);

    ESP_LOGW(TAG, "Зупиняємо роботу Telegram бота на час оновлення...");
    telegram_bot_paused = true; // Заморожуємо паралельні HTTP запити бота
    
    vTaskDelay(pdMS_TO_TICKS(1000)); // Даємо 1 секунду, щоб бот завершив поточний запит, якщо він ішов

    //Конфігурація розшифрування. Створюємо конфігурацію самого дешифратора esp_encrypted_img
    esp_decrypt_cfg_t decrypt_cfg = {
        .rsa_priv_key = (const char *)ota_private_key_pem_start,
        .rsa_priv_key_len = rsa_key_len, // Передаємо перевірену довжину
    };

    // 3. Ініціалізуємо контекст дешифрування (Це обов'язково для v6)
    esp_decrypt_handle_t decrypt_handle = esp_encrypted_img_decrypt_start(&decrypt_cfg);
    if (decrypt_handle == NULL) {
        ESP_LOGE(TAG, "Не вдалося ініціалізувати контекст розшифрування!");
        return;
    }

    // Конфігурація HTTP клієнта для GitHub
    esp_http_client_config_t http_config = {
        .url = GITHUB_BIN_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
        // Передаємо офіційний колбек дешифрування з бібліотеки
        .decrypt_cb = my_ota_decrypt_cb, 
        // Передаємо наш створений хендл як контекст користувача
        .decrypt_user_ctx = decrypt_handle,        
        // Вказуємо системі зарезервувати місце під криптографічний заголовок файлу
        .enc_img_header_size = esp_encrypted_img_get_header_size(),
    };

    ESP_LOGI(TAG, "Підключення до GitHub та завантаження бінарного файлу...");
    esp_err_t ota_ret = esp_https_ota(&ota_config); // Повністю автоматичний цикл запису у флеш

    // Очищення контексту після завершення OTA (незалежно від результату)
    esp_err_t clean_err = esp_encrypted_img_decrypt_end(decrypt_handle);
    if (clean_err != ESP_OK) {
        ESP_LOGW(TAG, "Помилка очищення контексту дешифратора: %s", esp_err_to_name(clean_err));
    }

    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA оновлення виконано успішно! Перезавантаження системи через 1 секунду...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Збій прошивки (можливо неправильний ключ на GitHub): %s. Перезапуск...", esp_err_to_name(ota_ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
}


 // Головна функція перевірки версії
bool  check_and_run_ota(void)
{
    ESP_LOGI(TAG, "Поточна версія прошивки на пристрої: %s", CURRENT_VERSION);
    ESP_LOGI(TAG, "Перевірка наявності оновлень на GitHub...");

    esp_http_client_config_t config = {
        .url = GITHUB_VERSION_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Не вдалося ініціалізувати HTTP клієнт");
        return false;
    }

    // Відкриваємо з'єднання та надсилаємо запит
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Помилка відкриття з'єднання: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return false;
    }

    // Отримуємо довжину контенту
    int content_length = esp_http_client_fetch_headers(client);
    if (content_length < 0) {
        ESP_LOGE(TAG, "Помилка читання заголовків");
        esp_http_client_cleanup(client);
        return false;
    }

    // Читаємо вміст файлу version.txt у буфер
    char remote_version[VERSION_BUF_SIZE] = {0};
    int read_bytes = esp_http_client_read(client, remote_version, VERSION_BUF_SIZE - 1);
    
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    if (read_bytes <= 0) {
        ESP_LOGE(TAG, "Отримано порожній файл версії або сталася помилка");
        return false;
    }

    // Очищаємо зчитаний рядок від можливих символів переносу рядка (\n, \r)
    remote_version[strcspn(remote_version, "\r\n")] = 0;

    ESP_LOGI(TAG, "Версія на GitHub: %s", remote_version);

    // Порівнюємо версії текстовим методом
    if (strcmp(remote_version, CURRENT_VERSION) != 0) {
        ESP_LOGW(TAG, "Знайдено нову версію прошивки! Запуск оновлення...");
        perform_https_ota();
        return false;
    } else {
        ESP_LOGI(TAG, "У вас встановлено найактуальнішу версію прошивки.");
        return true;
    }
}

/**  */
void validate_new_firmware(void) {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t ota_state;
    
    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            // Нова прошивка успішно запустилася і дойшла до цього рядка!
            // Робимо її постійною та основною
            esp_ota_mark_app_valid_cancel_rollback();
            ESP_LOGI("OTA", "Нову прошивку успішно валідовано та підтверджено! ✅");
        }
    }
}

/* // Функція запису прапорця OTA в NVS
static void save_ota_flag_to_nvs(int64_t chat_id)
{
    nvs_handle_t my_handle;
    // Відкриваємо простір імен "storage" у режимі читання/запису
    if (nvs_open("storage", NVS_READWRITE, &my_handle) == ESP_OK) {
        nvs_set_i8(my_handle, "ota_pending", 1);               // Встановлюємо прапорець
        nvs_set_i64(my_handle, "ota_chat_id", chat_id);        // Зберігаємо ID чату
        nvs_commit(my_handle);                                 // Фіксуємо зміни в пам'яті
        nvs_close(my_handle);
        ESP_LOGI(TAG, "Прапорець OTA успішно збережено в NVS.");
    } else {
        ESP_LOGE(TAG, "Не вдалося відкрити NVS для запису прапорця!");
    }
}*/
