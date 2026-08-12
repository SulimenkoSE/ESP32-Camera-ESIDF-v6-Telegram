#include <stdio.h>
#include "update_OTA.h"

#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_https_ota.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_camera.h" // Обов'язково для роботи з функціями камери

#include "esp_ota_ops.h"

#include "gpio_ctrl.h"

static const char *TAG = "GITHUB_OTA";

// Поточна версія прошивки у вашому коді
#define CURRENT_VERSION "1.0.0"

// Посилання на GitHub (замініть ВАШ_НІК та ІМ'Я_РЕПОЗИТОРІЮ)
#define GITHUB_VERSION_URL "https://raw.githubusercontent.com/SulimenkoSE/ESP32-Camera-ESIDF-v6-Telegram/main/version.txt"
#define GITHUB_BIN_URL     "https://raw.githubusercontent.com/SulimenkoSE/ESP32-Camera-ESIDF-v6-Telegram/main/station.bin"

#define VERSION_BUF_SIZE 32

extern volatile bool telegram_bot_paused;

// Функція безпосереднього завантаження бінарного файлу
static void perform_https_ota(void)
{
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

    // Конфігурація HTTP клієнта для GitHub
    esp_http_client_config_t http_config = {
        .url = GITHUB_BIN_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ESP_LOGI(TAG, "Підключення до GitHub та завантаження бінарного файлу...");
    esp_err_t ota_ret = esp_https_ota(&ota_config); // Повністю автоматичний цикл запису у флеш

    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA оновлення виконано успішно! Перезавантаження системи через 1 секунду...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Збій прошивки: %s. Перезапуск системи...", esp_err_to_name(ota_ret));
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
}

// Модифікована функція прошивки (додано аргумент chat_id)
static void perform_https_ota(int64_t chat_id)
{
    // 1. Зберігаємо інформацію про оновлення в NVS
    save_ota_flag_to_nvs(chat_id);

    ESP_LOGI(TAG, "Критично: Звільняємо пам'ять DRAM/IRAM перед OTA...");
    esp_camera_deinit(); 

    esp_http_client_config_t http_config = {
        .url = GITHUB_BIN_URL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    ESP_LOGI(TAG, "Підключення до GitHub та запис нової прошивки...");
    esp_err_t ota_ret = esp_https_ota(&ota_config);

    if (ota_ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA успішне! Перезавантаження...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "Збій прошивки: %s. Перезапуск системи...", esp_err_to_name(ota_ret));
        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    }
} */
