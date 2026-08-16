/* WiFi station Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#include "main.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"       // Коди помилок (err_t). Рекомендовано для супутніх типів помилок lwIP
#include "lwip/sys.h"       // Функції ОС, м'ютекси та потоки
#include "lwip/ip_addr.h"   // Містить базове визначення ip_addr_t. Визначення типу ip_addr_t та макросів для IP
#include "lwip/dns.h"       //функції DNS, які працюють з ip_addr_t

#include "ping/ping_sock.h"

#include "driver/gpio.h"

#include "esp_heap_caps.h"  //Куча з обмеженнями (heap_caps_malloc, heap_caps_get_free_size)  

#include "telegram.h"
#include "sntp_time.h"

#ifdef Camera
    #include "esp_camera.h"
    #include "camera_my.h"
#endif

#include "gpio_ctrl.h"

#include "update_OTA.h"

/* The examples use WiFi configuration that you can set via project configuration menu

   If you'd rather not, just change the below entries to strings with
   the config you want - ie #define EXAMPLE_WIFI_SSID "mywifissid"
*/
#define EXAMPLE_ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
#define EXAMPLE_ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define EXAMPLE_ESP_MAXIMUM_RETRY  CONFIG_ESP_MAXIMUM_RETRY

#if CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HUNT_AND_PECK
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HUNT_AND_PECK
#define EXAMPLE_H2E_IDENTIFIER ""
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_HASH_TO_ELEMENT
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_HASH_TO_ELEMENT
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#elif CONFIG_ESP_STATION_EXAMPLE_WPA3_SAE_PWE_BOTH
#define ESP_WIFI_SAE_MODE WPA3_SAE_PWE_BOTH
#define EXAMPLE_H2E_IDENTIFIER CONFIG_ESP_WIFI_PW_ID
#endif
#if CONFIG_ESP_WIFI_AUTH_OPEN
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_OPEN
#elif CONFIG_ESP_WIFI_AUTH_WEP
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WEP
#elif CONFIG_ESP_WIFI_AUTH_WPA_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA_WPA2_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA_WPA2_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WPA2_WPA3_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WPA2_WPA3_PSK
#elif CONFIG_ESP_WIFI_AUTH_WAPI_PSK
#define ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD WIFI_AUTH_WAPI_PSK
#endif

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;
SemaphoreHandle_t tg_http_mutex;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static uint8_t alarm_val = 0;
static const char *TAG = "wifi station";
static int s_retry_num = 0;

/***************************ПРОТОТИПИ*******************************/

/**
 * @brief Обробник подій Wi-Fi та IP
 * @param arg Аргумент обробника
 * @param event_base База подій (WIFI_EVENT або IP_EVENT)
 * @param event_id Ідентифікатор події
 * @param event_data Дані події
 */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data);

/**
 * @brief Ініціалізація Wi-Fi у режимі станції
 */
void wifi_init_sta(void);

/**
 * @brief Обробляє дані датчика та виводить інформацію про синхронізацію часу
 */
 void process_sensor_data(void);

/**
 * @brief Основна логіка програми для перевірки часу
 */
void my_main_logic_task(void *pvParameters);

/**
 * @brief Перевіряє стан PSRAM і виводить інформацію про використання пам'яті
 */
void check_psram_status(); 

/**
 * @brief Перевіряє стан внутрішньої пам'яті (MALLOC_CAP_INTERNAL) і виводить інформацію в лог.
 *        Універсальна функція, яка аналізує внутрішню RAM (смарт-пам'ять MALLOC_CAP_INTERNAL), 
 * оскільки саме з неї виділяється пам'ять під HTTP-буфери
 */
void heap_monitor_task(void *pvParameters) ;

/**
 * @brief Отримати значення які встановлено для сигналізації
 * @return Значення сигналізації (0 або 1)
 */
uint8_t get_alarm_val(void) ;

/**
 * @brief Функція для зміни значення сигналізації
 */
void set_alarm_val(uint8_t new_val);

/**
 * @brief Reads alarm value from non-volatile storage
 * @return Alarm value from NVS
 */
uint8_t read_alarm_val(void);

/**
 * @brief Записує значення сигналізації у енергонезалежну пам'ять
 * @param new_val Нове значення сигналізації для запису
 */
void write_alarm_val(uint8_t new_val);

/**
 * @brief Ви можете вивести поточні DNS-сервери в консоль одразу після підключення до Wi-Fi
 */
void print_current_dns_servers(void);

/**
 * @brief Завдання для обробки черги дій
 * @param pvParameters Параметри завдання
 */
void action_queue_task(void *pvParameters);

 /**
  * @brief Зчитування значення alarm_val з NVS пам'яті
  * @return uint8_t - значення стану тривоги (0 або 1)
  */
 uint8_t read_alarm_val(void);

/**
 * @brief Функція для запису нового значення (приймає 0 або 1) з NVS
 * @param new_val 
 */
void write_alarm_val(uint8_t new_val);
/***************************ПРОТОТИПИ*******************************/

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Додайте це в обробник подій Wi-Fi (де перехоплюється IP_EVENT_STA_GOT_IP)
        ESP_LOGI("NET", "Моя IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI("NET", "Шлюз (GW): " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI("NET", "Маска (MK): " IPSTR, IP2STR(&event->ip_info.netmask));
    }
}


void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            /* Authmode threshold resets to WPA2 as default if password matches WPA2 standards (password len => 8).
             * If you want to connect the device to deprecated WEP/WPA networks, Please set the threshold value
             * to WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK and set the password with length and format matching to
             * WIFI_AUTH_WEP/WIFI_AUTH_WPA_PSK standards.
             */
            .threshold.authmode = ESP_WIFI_SCAN_AUTH_MODE_THRESHOLD,
            .sae_pwe_h2e = ESP_WIFI_SAE_MODE,
            .sae_h2e_identifier = EXAMPLE_H2E_IDENTIFIER,
#ifdef CONFIG_ESP_WIFI_WPA3_COMPATIBLE_SUPPORT
            .disable_wpa3_compatible_mode = 0,
#endif
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    //Вимкнення Modem-sleep (прибирає мікрозасинання та пінг-спайки).
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "connected to ap SSID:%s password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s, password:%s",
                 EXAMPLE_ESP_WIFI_SSID, EXAMPLE_ESP_WIFI_PASS);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

void print_current_dns_servers(void) {
    for (uint8_t i = 0; i < DNS_MAX_SERVERS; i++) {
        const ip_addr_t *dns_ip = dns_getserver(i);
        if (ip_addr_isany(dns_ip)) {
            ESP_LOGI("DNS_INFO", "DNS Слот %d: порожній", i);
        } else {
            ESP_LOGI("DNS_INFO", "DNS Слот %d: %s", i, ipaddr_ntoa(dns_ip));
        }
    }
}

#ifdef DNS_TEST

static const char *TAG_DNS = "dns_test";

void check_dns_servers(void) {
    // Цикл по всім можливим слотам DNS-серверів
    for (int i = 0; i < DNS_MAX_SERVERS; i++) {
        const ip_addr_t *dns_ip = dns_getserver(i);

        // 1. Перевіряємо, чи вказівник не NULL і чи адреса не є порожньою (0.0.0.0)
        if (dns_ip != NULL && !ip_addr_isany(dns_ip)) {
            // 2. Безпечно перетворюємо в рядок та виводимо в лог
            ESP_LOGI(TAG_DNS, "DNS Server [%d]: %s", i, ipaddr_ntoa(dns_ip));
        } else {
            ESP_LOGW(TAG_DNS, "DNS Server [%d]: Not set (or empty)", i);
        }
    }
}

// 1. Створюємо функцію зворотного виклику (Callback)
// Вона виконається автоматично, коли DNS-сервер надішле відповідь
void dns_callback(const char *name, const ip_addr_t *ipaddr, void *callback_arg) {
    if (ipaddr != NULL) {
        // Успішно знайшли IP
        ESP_LOGI(TAG_DNS, "Callback: Успішно розпізнано %s -> %s", name, ipaddr_ntoa(ipaddr));
    } else {
        // Помилка (наприклад, хост не існує або таймаут)
        ESP_LOGE(TAG_DNS, "Callback: Не вдалося розпізнати хост %s", name);
    }
}

// 2. Основна функція для запуску тесту
void test_dns_resolution(const char *hostname) {
    ip_addr_t resolved_ip;
    
    ESP_LOGI(TAG_DNS, "Запуск DNS-запиту для: %s", hostname);

    // Викликаємо функцію lwIP
    err_t err = dns_gethostbyname(hostname, &resolved_ip, dns_callback, NULL);

    if (err == ERR_OK) {
        // Сценарій 1: Результат був у кеші, callback викликатися НЕ буде
        ESP_LOGI(TAG_DNS, "[КЕШ]: Хост %s миттєво знайдено: %s", hostname, ipaddr_ntoa(&resolved_ip));
    } 
    else if (err == ERR_INPROGRESS) {
        // Сценарій 2: Запит відправлено в мережу. Чекаємо на callback
        ESP_LOGI(TAG_DNS, "Запит відправлено, очікуємо відповіді від сервера...");
    } 
    else {
        // Сценарій 3: Сталася миттєва помилка (наприклад, DNS не ініціалізовано)
        ESP_LOGE(TAG_DNS, "Помилка ініціалізації DNS запиту: %d", err);
    }
}

/*
Найчастіше пристрій отримує IP-адресу від Wi-Fi (або роутера), 
але сам роутер може не випускати пристрій далі локальної мережі, 
або в мережі заблоковано UDP-порт 53 (DNS).
Спробуйте виконати звичайний ICMP-пінг до 8.8.8.8 прямо з коду, 
щоб дізнатися, чи бачить ESP32 зовнішній світ. Для цього в ESP-IDF є вбудована консольна утиліта:
*/
void test_ping(void) {
    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ipaddr_aton("8.8.8.8", &ping_config.target_addr);
    
    esp_ping_callbacks_t cbs = {0}; // Можна залишити порожніми для базового виведення в консоль
    esp_ping_handle_t ping_handle;
    esp_ping_new_session(&ping_config, &cbs, &ping_handle);
    esp_ping_start(ping_handle);
}

#endif

void process_sensor_data(void) {
    char time_buf[32];

    // 2. Викликаємо функцію за запитом
    bool is_valid = get_current_time_string(time_buf, sizeof(time_buf));

    if (is_valid) {
        ESP_LOGI("SENSOR", "[%s] Дані датчика успішно збережено", time_buf);
    } else {
        ESP_LOGW("SENSOR", "[%s] Дані записані з несинхронізованим часом", time_buf);
    }
}

void my_main_logic_task(void *pvParameters) {
    struct tm current_time;

    while (1) {
        bool time_is_accurate = get_current_time_struct(&current_time);

        if (time_is_accurate) {
            ESP_LOGI("APP", "Логіка: Час ТОЧНИЙ: %02d:%02d:%02d", 
                     current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
        } else {
            ESP_LOGW("APP", "Логіка: Час НЕ ТОЧНИЙ (дефолтний): %02d:%02d:%02d", 
                     current_time.tm_hour, current_time.tm_min, current_time.tm_sec);
        }
        
        // Виконуємо корисну роботу пристрою кожні 10 секунд
        vTaskDelay(pdMS_TO_TICKS(10000)); 
    }
}

// фонова задача в app_main, яка почекає 5 хвилин, зробить замір, а потім буде повторювати його через 1 хвиилну
static const char *TAG_MEM = "MEM_MONITOR";

void check_heap_status(void) {
    // 1. Загальна вільна пам'ять у кучі
    uint32_t free_heap = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    
    // 2. Найменший рівень вільної пам'яті, який взагалі фіксувався з моменту старту (Watermark)
    uint32_t min_free_heap = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
    
    // 3. Найбільший суцільний шматок пам'яті (якщо він менший за ваш буфер, malloc видасть помилку)
    uint32_t max_block = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG_MEM, "--- Стан кучі ---");
    ESP_LOGI(TAG_MEM, "Вільна пам'ять: %lu байт (%.2f КБ)", free_heap, (float)free_heap / 1024.0);
    ESP_LOGI(TAG_MEM, "Історичний мінімум (Watermark): %lu байт", min_free_heap);
    ESP_LOGI(TAG_MEM, "Найбільший доступний блок: %lu байт", max_block);
    
    // Сигналізуємо, якщо пам'яті критично мало (наприклад, менше 30 КБ)
    if (free_heap < 30000) {
        ESP_LOGE(TAG_MEM, "⚠️ КРИТИЧНО: Можливий витік пам'яті (Memory Leak)!");
    }
}

void heap_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG_MEM, "Моніторинг запущено. Перший замір буде через 5 хвилин...");
    
    // 1. Очікування 5 хвилин (5 хв * 60 сек * 1000 мс)
    vTaskDelay(pdMS_TO_TICKS(5 * 60 * 1000));
    
    // Перша перевірка рівно через 5 хвилин
    ESP_LOGW(TAG_MEM, "=== КОНТРОЛЬНИЙ ЗАМІР (5 ХВИЛИН РОБОТИ) ===");
    check_heap_status();

    // 2. Подальший регулярний моніторинг щохвилини (опціонально, для пошуку витоків)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(60 * 1000)); // 1 хвилина
        check_heap_status();
    }
}

uint8_t get_alarm_val(void) {
    return alarm_val;
}

void set_alarm_val(uint8_t new_val) {
    alarm_val = (new_val > 0) ? 1 : 0; 
    write_alarm_val(alarm_val);
}

static QueueHandle_t action_queue = NULL;


void action_queue_task(void *pvParameters){   
    //static uint8_t status_pin = 0;
    telegram_queue_msg_t msg;
    while (1) {
        // Чекаємо повідомлення з черги (блокуючий виклик, не їсть процесор)
        //Цей код спрацює лише тоді коли alarm_val = 1, а значення status_pin_sensor зміниться 
        if (xQueueReceive(action_queue, &msg, portMAX_DELAY) == pdTRUE) {
                if (msg.type == TG_TYPE_LED) {
                 
                
                if (msg.value == 1) {
                    ESP_LOGW("ACTION_QUEUE", "Переферію увімкнуто!");
                    // Додаткові дії при виявленні руху відправка сповіщення в телеграм
                    //text_QueueSend("Виявлено рух!");
                } else {
                    ESP_LOGI("ACTION_QUEUE", "Переферію вимкнуто");
                    // Додаткові дії при відсутності руху
                } 
            } else {
                ESP_LOGW("ACTION_QUEUE", "Отримано повідомлення з черги дій, але тип не ACTION: %d", msg.type);
            }
        }
    }
}
/********************Робота з NVS***********************************/
 uint8_t read_alarm_val(void) {
    nvs_handle_t my_handle;
    uint8_t alarm_val = 0; // Значення за замовчуванням

    // 1. Пробуємо відкрити у режимі лише для читання
    esp_err_t err = nvs_open("storage", NVS_READONLY, &my_handle);
    
    // 2. Якщо простір імен ще не створено (чистий флеш)
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Сховище порожнє. Створюємо простір імен...");
        
        // Відкриваємо в режимі READWRITE (це автоматично створить простір імен "storage")
        err = nvs_open("storage", NVS_READWRITE, &my_handle);
        if (err == ESP_OK) {
            // Записуємо значення за замовчуванням (0), щоб ключ з'явився в базі
            nvs_set_u8(my_handle, "alarm_key", alarm_val);
            nvs_commit(my_handle); // Фіксуємо зміни
            nvs_close(my_handle);  // Закриваємо
            
            return alarm_val; // Повертаємо 0
        }
    }

    // 3. Якщо відкрилося штатно або вже існує
    if (err == ESP_OK) {
        err = nvs_get_u8(my_handle, "alarm_key", &alarm_val);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "Ключ не знайдено, використовуємо 0");
        }
        nvs_close(my_handle);
    } else {
        ESP_LOGE(TAG, "Критична помилка відкриття NVS (%s)", esp_err_to_name(err));
    }
    
    return alarm_val;
}

void write_alarm_val(uint8_t new_val) {
    nvs_handle_t my_handle_nvs;

    // Обмежуємо значення лише 0 або 1 для безпеки
    new_val = (new_val > 0) ? 1 : 0;

    // Відкриваємо простір імен "storage" у режимі READWRITE
    esp_err_t err = nvs_open("storage", NVS_READWRITE, &my_handle_nvs);
    if (err == ESP_OK) {
        // Записуємо нове значення у буфер
        err = nvs_set_u8(my_handle_nvs, "alarm_key", new_val);
        if (err == ESP_OK) {
            // Обов'язково фіксуємо зміни у фізичній флеш-пам'яті
            err = nvs_commit(my_handle_nvs);
            if (err == ESP_OK) {
                ESP_LOGI("ALARM_SYSTEM", "Успішно збережено alarm_val = %d", new_val);
            }
        }
        nvs_close(my_handle_nvs);
    } else {
        ESP_LOGE("ALARM_SYSTEM", "Помилка відкриття NVS для запису (%s)", esp_err_to_name(err));
    }
}
/*******************************************************************/

/**
 * @brief Основна функція програми.
 */
void app_main(void)
{
    //Initialize NVS Ініціалізація NVS (робиться ОДИН раз при запуску контролера)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    if (CONFIG_LOG_MAXIMUM_LEVEL > CONFIG_LOG_DEFAULT_LEVEL) {
        /* If you only want to open more logs in the wifi module, you need to make the max level greater than the default level,
         * and call esp_log_level_set() before esp_wifi_init() to improve the log level of the wifi module. */
        esp_log_level_set("wifi", CONFIG_LOG_MAXIMUM_LEVEL);
    }

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    wifi_init_sta();
    // Чекаємо стабільного підключення (імітація затримки)
    vTaskDelay(pdMS_TO_TICKS(5000));

    //Валідація встановденной версії прошивки
    validate_new_firmware();

#ifdef DNS_TEST
    // Лише тепер міняємо DNS і робимо запит!
    //ip_addr_t my_dns;
    //IP_ADDR4(&my_dns, 8, 8, 8, 8);
    //dns_setserver(0, &my_dns);
    //ESP_LOGI(TAG, "check_dns_servers() - перевірка DNS-серверів після зміни:");
    //check_dns_servers();
    //vTaskDelay(pdMS_TO_TICKS(5000));
    //test_ping();
    ESP_LOGI(TAG, "test_dns_resolution() - перевірка DNS-розпізнавання:");
    test_dns_resolution("google.com"); */
    
    /* print_current_dns_servers();xTaskC reatePinnedToCore(&http_test_task, "http_test_task", 8192 * 4, NULL, 5, NULL, 1);
    vTaskDelay(pdMS_TO_TICKS(500));
    start_time_sync();*/


    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(30000));
        process_sensor_data();
        vTaskDelay(pdMS_TO_TICKS(30000));
        check_night_mode();
    }   
#endif    
    
   // Запуск нашого інтелектуального алгоритму синхронізації часу дати та роу
    time_utils_init_with_timeout();

    // Отримуємо загальний розмір доступної PSRAM
    size_t psram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);

    if (psram_size > 0)
    {
        ESP_LOGI(TAG, "PSRAM знайдено! Розмір: %d байт", psram_size);
    }
    else
    {
        ESP_LOGE(TAG, "PSRAM відсутня або не активована у menuconfig.");
    }

    // Цей код виконається або через 0.5 сек (якщо інтернет швидкий), або рівно через 30 секунд
    ESP_LOGI("MAIN", "Конфігурація BLINK GPIO пристрою!");

    gpio_ctrl_init_pin(CONFIG_BLINK_GPIO, GPIO_MODE_OUTPUT, GPIO_PULLUP_DISABLE, GPIO_PULLDOWN_DISABLE);
    gpio_ctrl_set_level(CONFIG_BLINK_GPIO, 0); // Встановлюємо початковий стан GPIO у LOW   
    
    #if CONFIG_ENABLE_FLASH
        ESP_LOGI("MAIN", "Конфігурація FLASH GPIO пристрою!");
        gpio_ctrl_init_pin(CONFIG_GPIO_FLASH, GPIO_MODE_OUTPUT, GPIO_PULLUP_ONLY, GPIO_PULLDOWN_DISABLE);
        gpio_ctrl_set_level(CONFIG_GPIO_FLASH, 0); // Встановлюємо початковий стан GPIO у LOW
    #endif
    
    // Створюємо чергу на 10 елементів типу int для обробки дій від телеграм-бота та інших подій на певномму пини
    action_queue = xQueueCreate(10, sizeof(int));
    if (action_queue == NULL)
    {
        ESP_LOGE("MAIN", "Error creating the queue");
        return;
    }else{
        ESP_LOGI("MAIN", "Черга дій успішно створена!");
    }

    // Створюємо чергу Telegram. 
    // Вона МАЄ бути створена ДО запуску задачі, щоб уникнути помилок доступу!
    telegram_queue = xQueueCreate(5, sizeof(telegram_queue_msg_t));
    if (telegram_queue == NULL) {
        ESP_LOGE("MAIN", "Не вдалося створити чергу Telegram!");
        return;
    }else{
        ESP_LOGI("MAIN", "Черга Telegram успішно створена!");
    }

#ifdef Camera
    ret = camera_init();
    if (ret != ESP_OK) {
        ESP_LOGE("MAIN", "Помилка ініціалізації камери");
        vTaskDelay(pdMS_TO_TICKS(1000));
        abort();
    }else{
        ESP_LOGI("MAIN", "Камера успішно ініціалізована/ Чекаемо 12 секунд.");
    }
    
    // Даємо 10 секунд на те, щоб Wi-Fi підключився до роутера і мережа стала стабільною
    vTaskDelay(pdMS_TO_TICKS(10000));

#endif
    
    // Створюємо задачу для обробки черги дій (action_queue)
     BaseType_t xReturned = xTaskCreatePinnedToCore(
                                                    &action_queue_task, 
                                                    "action_queue_task", 
                                                    2048, 
                                                    NULL, 
                                                    4, 
                                                    NULL, 
                                                    1);
    if (xReturned != pdPASS) {
        ESP_LOGE("MAIN", "Не вдалося створити задачу Telegram Worker з функцією telegram_queue_task!");
    }else{
        ESP_LOGE("MAIN", "Cтворeна задача action_queue_task з функцією обрробки action_queue_task!");
    }

    //Для нормальної роботи камери та HTTP-запитів створюємо м'ютекс, щоб уникнути одночасного доступу до HTTP-запитів  
    tg_http_mutex = xSemaphoreCreateMutex();

    // Запускаємо задачу-менеджер Telegram
    xReturned = xTaskCreate(
                            &telegram_queue_task,   // Функція задачі
                            "tg_worker_task",       // Текстова назва для відладки
                            8192,                   // Розмір стеку у байтах (8 КБ)
                            NULL,                   // Параметри, що передаються в задачу
                            5,                      // Пріоритет задачі (середній)
                            NULL                    // Хендл задачі (не потрібен, якщо не видаляємо її)
                        );

    if (xReturned != pdPASS) {
        ESP_LOGE("MAIN", "Не вдалося створити задачу Telegram Worker з функцією telegram_queue_task!");
    }else{
        ESP_LOGE("MAIN", "Cтворeна задача Telegram Worker з функцією telegram_queue_task!");
    }
    // Даємо 10 секунд на те, щоб Wi-Fi підключився до роутера і мережа стала стабільною
    vTaskDelay(pdMS_TO_TICKS(10000));
    
    // запускаємо задачу бота
    xReturned = xTaskCreate(
                            &telegram_bot_task, 
                            "telegram_bot_task", 
                            8192, 
                            NULL, 
                            4, 
                            NULL);
    if (xReturned != pdPASS) {
        ESP_LOGE("MAIN", "Не вдалося створити задачу telegram_bot_task з функцією getupdates!");
    }else{
        ESP_LOGE("MAIN", "Cтворeна задача telegram_bot_task з функцією getupdates");
    }
     vTaskDelay(pdMS_TO_TICKS(500)); // Додатково чекаємо 0.1 секунду для стабілізації камери
    
    // Запускаэмо сенсор

    // Зчитуємо дані при старті системи
    alarm_val = read_alarm_val();
    ESP_LOGI(TAG, "--- СТАРТ СИСТЕМИ. Поточне значення alarm_val: %d ---", alarm_val);

    init_gpio_interrupt();
    //якщо сигналізація вимкнена зупиняємо interrupt
    if(alarm_val == 0){
        stop_gpio_interrupt(CONFIG_SENSOR_GPIO);
    }

    
    void check_psram_status() {
    // 1. Отримуємо загальний розмір PSRAM, який бачить система
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    
    // 2. Отримуємо кількість вільної пам'яті в PSRAM прямо зараз
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    // 3. Рахуємо, скільки вже зайнято
    size_t used_psram = total_psram - free_psram;
    
    // 4. Додатково: дізнаємося розмір найбільшого суцільного шматка (важливо для malloc)
    size_t max_chunk = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

    ESP_LOGI("MEMORY", "=== СТАН PSRAM ===");
    ESP_LOGI("MEMORY", "Загалом:   %d КБ", total_psram / 1024);
    ESP_LOGI("MEMORY", "Зайнято:   %d КБ", used_psram / 1024);
    ESP_LOGI("MEMORY", "Вільно:    %d КБ", free_psram / 1024);
    ESP_LOGI("MEMORY", "Макс. блок: %d КБ (найбільший суцільний шматок)", max_chunk / 1024);
    ESP_LOGI("MEMORY", "=================");
}

    // Запускаємо задачу моніторингу з низьким пріоритетом
    xTaskCreate(&heap_monitor_task, "heap_monitor_task", 3072, NULL, 1, NULL); 
}
