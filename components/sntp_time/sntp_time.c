#include <stdio.h>
#include "sntp_time.h"

#include <time.h>
#include <sys/time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "sntp_time";

// Семафор для початкового швидкого очікування
static SemaphoreHandle_t s_time_sync_sem = NULL;
// Прапорець, який вказує, чи була колись виконана успішна синхронізація
static bool s_is_time_synchronized = false;

/**
 * Callback function for time synchronization notification
 * @param tv pointer to timeval structure containing the synchronized time  
 */
// Callback: викликається lwIP фоново при кожній успішній відповіді NTP сервера
static void time_sync_notification_cb(struct timeval *tv) {
    ESP_LOGI(TAG, "Синхронізація успішна! Час оновлено.");
    s_is_time_synchronized = true;
    
    // Якщо семафор ще існує (йде перша ініціалізація), розблоковуємо потік
    if (s_time_sync_sem != NULL) {
        xSemaphoreGive(s_time_sync_sem);
    }
}

// Фонове завдання FreeRTOS для періодичного примусового оновлення, якщо інтернет з'явився пізніше
static void sntp_retry_task(void *pvParameters) {
    while (1) {
        // Чекаємо 5 хвилин (5 * 60 * 1000 мс)
        vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
        
        if (!s_is_time_synchronized) {
            ESP_LOGW(TAG, "Попередній запит не вдався. Повторна спроба синхронізації з NTP...");
            // Перезапуск SNTP модуля для надсилання нового свіжого запиту в мережу
            esp_sntp_stop();
            esp_sntp_init();
        } else {
            // Якщо час уже хоч раз синхронізовано успішно, цей потік більше не потрібен
            ESP_LOGI(TAG, "Час стабільний. Фоновий потік повторів завершує роботу.");
            vTaskDelete(NULL);
        }
    }
}

void time_utils_init_with_timeout(void) {
    s_time_sync_sem = xSemaphoreCreateBinary();
    if (s_time_sync_sem == NULL) {
        ESP_LOGE(TAG, "Не вдалося створити семафор!");
        return;
    }

    ESP_LOGI(TAG, "Ініціалізація SNTP...");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "://google.com");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();

    // Налаштовуємо часовий пояс України
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    ESP_LOGI(TAG, "Очікування первинної синхронізації часу (таймаут 30 секунд)...");
    
    // Очікуємо семафор максимум 30 секунд
    if (xSemaphoreTake(s_time_sync_sem, pdMS_TO_TICKS(30000)) == pdTRUE) {
        ESP_LOGI(TAG, "Час успішно отримано в межах ліміту!");
    } else {
        ESP_LOGE(TAG, "Таймаут 30 сек вичерпано! Інтернет відсутній або сервер не відповів.");
        ESP_LOGW(TAG, "Запускаємо пристрій на дефолтному / старому часі з RTC.");
        
        // Створюємо фоновий потік, який кожні 5 хвилин перевірятиме інтернет та пробуватиме оновитися
        xTaskCreate(sntp_retry_task, "sntp_retry", 3072, NULL, 3, NULL);
    }

    // Видаляємо тимчасовий семафор початкового старту
    vSemaphoreDelete(s_time_sync_sem);
    s_time_sync_sem = NULL;
}

/**
 * Initialize SNTP and set up time synchronization       
*/
void initialize_sntp(void) {
    ESP_LOGI(TAG, "Ініціалізація модуля SNTP...");
    
    // 1. Встановлюємо режим роботи (по запиту через мережу)
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    
    // 2. Вказуємо NTP-сервер (pool.ntp.org автоматично підбере найближчий до вас сервер)
    esp_sntp_setservername(0, "pool.ntp.org");
    
    // 3. Реєструємо callback для сповіщення про успішну синхронізацію
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    
    // 4. Запускаємо службу
    esp_sntp_init();
} 

/**
 * Wait for time synchronization to complete and print the current time
 */
void wait_and_print_time(void) {
    // 5. Встановлюємо часовий пояс для України (Kyiv)
    // EET (Eastern European Time) = GMT+2 взимку, GMT+3 влітку (EEST)
    setenv("TZ", "EET-2EEST,M3.5.0/3,M10.5.0/4", 1);
    tzset();

    int retry = 0;
    const int retry_count = 10;
    
    // Чекаємо, поки статус синхронізації зміниться на COMPLETED
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry <= retry_count) {
        ESP_LOGI(TAG, "Очікування синхронізації часу... (%d/%d)", retry, retry_count);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // 6. Отримуємо та виводимо поточний час
    time_t now;
    struct tm timeinfo;
    
    time(&now); // Зчитуємо поточний Unix-час з RTC мікроконтролера
    localtime_r(&now, &timeinfo); // Конвертуємо у локальний формат з урахуванням TZ

    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG, "Поточний локальний час: %s", strftime_buf);
}

// Функція для запуску з вашого основного коду
void start_time_sync(void) {
    initialize_sntp();
    wait_and_print_time();
}


/**
 * @brief Отримує поточний локальний час у вигляді відформатованого рядка.
 *        Функція працює автономно на основі внутрішнього RTC без доступу до інтернету.
 * 
 * @param buffer Вказівник на масив char, куди буде записано час.
 * @param max_len Максимальний розмір буфера.
 * @return true якщо час синхронізовано (рік >= 2020), false якщо час дефолтний (1970 рік).
 */
bool get_current_time_string(char *buffer, size_t max_len) {
    time_t now;
    struct tm timeinfo;

    // Зчитуємо Unix-час з внутрішнього RTC
    time(&now);
    // Конвертуємо у локальний формат з урахуванням TZ
    localtime_r(&now, &timeinfo);

    // Форматуємо час у рядок: "ГОДИНИ:ХВИЛИНИ:СЕКУНДИ ДД.ММ.РРРР"
    strftime(buffer, max_len, "%H:%M:%S %d.%m.%Y", &timeinfo);

    // Перевіряємо, чи рік більший або рівний 2020 (120 років з 1900 року в struct tm)
    if (timeinfo.tm_year < 120) {
        ESP_LOGW(TAG, "Час запитано, але RTC ще не синхронізовано з мережею!");
        return false;
    }
    return true;
}

/**
 * @brief Заповнює структуру tm поточними даними часу з RTC.
 * 
 * @param target_time_struct Вказівник на структуру, яку треба заповнити.
 * @return true якщо час синхронізовано, false якщо ні.
 */
bool get_current_time_struct(struct tm *target_time_struct){
    
    if (target_time_struct == NULL) {
        return false; // Захист від передачі порожнього вказівника
    }
    time_t now;
    // Зчитуємо Unix-час з внутрішнього RTC
    time(&now);

    // Конвертуємо Unix-час і записуємо дані ПРЯМО в структуру за адресою target_time_struct
    // Ця функція автоматично враховує часовий пояс (якщо встановлено setenv("TZ",...))
    localtime_r(&now, target_time_struct);

    // Перевіряємо, чи рік більший або рівний 2020 (120 років з 1900 року в struct tm)
    if (target_time_struct->tm_year < 120) {
        ESP_LOGW(TAG, "Час запитано, але RTC ще не синхронізовано з мережею!");
        return false;
    }
    return s_is_time_synchronized;
}

/**
 * @brief Перевіряє, чи поточний час знаходиться в межах нічного режиму (22:00 - 06:00).
 *        Використовує функцію get_current_time_struct() для отримання часу.
 *        Якщо час ще не синхронізовано, виводить попередження. 
 *        Тепер у будь-якому іншому файлі проекту ви створюєте локальну структуру і передаєте 
 *        її адресу (через оператор &) у функцію.
 */
void check_night_mode(void) {
    // 1. Створюємо порожню структуру для часу в цьому файлі
    struct tm current_time;

    // 2. Передаємо її адресу (&current_time) у функцію модуля
    bool is_synchronized = get_current_time_struct(&current_time);

    if (is_synchronized) {
        // 3. Дані успішно записані, тепер можемо використовувати окремі поля:
        int hours = current_time.tm_hour;
        int minutes = current_time.tm_min;
        int day = current_time.tm_mday;
        int month = current_time.tm_mon + 1; // У struct tm місяці рахуються від 0 до 11
        int year = current_time.tm_year + 1900; // Роки рахуються від 1900

        ESP_LOGI("LOGIC", "Числові дані: %02d:%02d (%02d.%02d.%d)", hours, minutes, day, month, year);

        // Приклад використання в автоматизації (нічний режим)
        if (hours >= 22 || hours < 6) {
            ESP_LOGI("LOGIC", "Активовано нічний режим енергозбереження.");
        } else {
            ESP_LOGI("LOGIC", "Денний режим роботи.");
        }
    } else {
        ESP_LOGW("LOGIC", "Неможливо виконати логіку: час ще не синхронізовано з інтернету!");
    }
}