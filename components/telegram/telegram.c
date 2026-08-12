#include <stdio.h>
#include "main.h"

#include "telegram.h"
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_wifi.h"
#include "esp_http_client.h"
#include "driver/gpio.h"

#include "esp_crt_bundle.h" // Обов'язково для валідації сертифікатів Telegram

#include "cJSON.h"  // Важливо: літера J має бути ВЕЛИКОЮ

#include "camera_my.h"
#include "gpio_ctrl.h"
#include "update_OTA.h"

/*Telegram configuration*/
#define TELEGRAM_HOST CONFIG_TELEGRAM_HOST         // "https://api.telegram.org"

#define HANDLE_MESSAGES CONFIG_HANDLE_MESSAGES     // 1

#define TOKEN CONFIG_TOKEN
#define CHAT_ID CONFIG_CHAT_ID
#define CHAT_ID1 CONFIG_CHAT_ID1

static const char *_URL_OFFSET = "https://api.telegram.org/bot" TOKEN "/getUpdates?offset=%d";
static const char *_URL_POST_MESSAGE = "https://api.telegram.org/bot" TOKEN "/sendMessage";
static const char *_URL_POST_PHOTO = "https://api.telegram.org/bot" TOKEN "/sendPhoto";

/* TAGs for the system*/
static const char *TAG = "HTTP_CLIENT Handler";
static int32_t last_update_id = 0;
volatile bool telegram_bot_paused = false;

// Формуємо текст повідомлення (використовуємо \n для нових рядків)
const char *text_start =     "Система успішно запущена!\n\n"
                             "Ось основні функції:\n\n"
                             "/status\n\n"
                             "/update\n\n"
                             "/photo\n\n"
                             "/led_on\n\n"
                             "/led_off\n\n"
                             "/alarm_on\n\n"
                             "/alarm_off"
                             "\",\"parse_mode\":\"HTML"; 
/*
Коли захочете відправити будь-яке інше повідомлення з переносом рядків \n 
або жирним текстом <b>...</b>, завжди дописуйте цей рядок \",\"parse_mode\":\"HTML 
в самому кінці тексту.
*/
// Черга зберігає самі структури, а не вказівники на них!
QueueHandle_t telegram_queue;
extern SemaphoreHandle_t tg_http_mutex; // Тепер цей файл знає про існування м'ютексу
extern QueueHandle_t action_queue; // Тепер цей файл знає про існування черги дій

// Оголошуємо клієнт як статичний, щоб він жив між викликами функції
static esp_http_client_handle_t global_tg_client = NULL;

/** 
 *  @brief HTTP event handler for the ESP HTTP client
 *  @param evt Pointer to the HTTP client event structure
 *  @return ESP_OK on success, ESP_FAIL on failure
 */
static esp_err_t _http_event_handler(esp_http_client_event_t *evt) {
    // Отримуємо доступ до нашої структури, яку ми передали з основної функції
    http_response_t *res = (http_response_t *)evt->user_data;

    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (evt->client == NULL || evt->data == NULL || evt->data_len <= 0 || res == NULL) {
                break; 
            }

            // 1. ЗАХИСТ: Перевіряємо загальний ліміт пам'яті (4 КБ для Telegram — чудово)
            if (res->buffer_len + evt->data_len > 4096) {
                ESP_LOGW("TG_EVENT", "Відповідь перевищує ліміт 4КБ, ігноруємо залишок даних");
                if (res->buffer != NULL) {
                    free(res->buffer);
                    res->buffer = NULL; // <--- ЗАХИСТ
                }
                res->buffer_len = 0;
                
                return ESP_FAIL; // Перериває виконання perform() та закриває сокет!
            }

            // 2. УНІВЕРСАЛЬНИЙ REALLOC: Працює ідеально для будь-якого типу передачі (і chunked, і звичайний)
            char *new_ptr = realloc(res->buffer, res->buffer_len + evt->data_len + 1);
            if (new_ptr == NULL) {
                ESP_LOGE("TG_EVENT", "Брак пам'яті для realloc відповіді!");
                // Якщо забракло пам'яті, не занулюємо res->buffer вручну, 
                // основна функція сама очистить те, що встигло завантажитись.
                return ESP_ERR_NO_MEM;
            }
            res->buffer = new_ptr;

            // 3. БЕЗПЕЧНЕ КОПІЮВАННЯ ТА ТЕРМІНАЦІЯ СТРОКИ
            memcpy(res->buffer + res->buffer_len, evt->data, evt->data_len);
            res->buffer_len += evt->data_len;
            res->buffer[res->buffer_len] = '\0'; // Гарантований C-string для cJSON
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI("TG_EVENT", "HTTP_EVENT_DISCONNECTED: Робимо вихід.");
            if (res != NULL && res->buffer != NULL) {
                free(res->buffer);
                res->buffer = NULL; // <-- КРИТИЧНО! Тепер free(NULL) в тасці буде безпечним
                res->buffer_len = 0;
            }
            break;
            
        default:
            break;
    }
    return ESP_OK; 
}


/* 04082026 1517*/
/** @brief Ініціалізує клієнт Telegram/ // Функція ініціалізації (викликайте її ОДИН РАЗ при старті системи, після підключення до Wi-Fi)
 *  @return true, якщо успішно, false в іншому випадку
 */
bool init_telegram_client(void) {
    if (global_tg_client != NULL) return true;

    esp_http_client_config_t config = {
        .url = _URL_POST_PHOTO,
        .method = HTTP_METHOD_POST,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .skip_cert_common_name_check = false,
        .use_global_ca_store = false,
        .timeout_ms = 15000, // Для Keep-Alive 5 секунд більше ніж достатньо
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = _http_event_handler,
        .keep_alive_enable = true, // Тепер це РЕАЛЬНО буде працювати
        .user_data = NULL, // Ми будемо передавати структуру http_response_t під час кожного виклику perform()
    };

    global_tg_client = esp_http_client_init(&config);
    return (global_tg_client != NULL);
}


/**
 *  @brief Функція відправки тексту в чергу
 *  @param text
 */
void text_QueueSend(const char *text) {
    if (text == NULL) return;
    if (telegram_queue == NULL) {
        ESP_LOGE("TG_QUEUE", "Помилка: Черга Telegram ще не створена!");
        return;
    }

    // 1. Створюємо об'єкт повідомлення для черги
    telegram_queue_msg_t msg;
    msg.type = TG_TYPE_TEXT;
    msg.value = 0; // Не використовується для тексту

    // 2. Виділяємо пам'ять під рядок (+1 для нуль-термінатора '\0')
    msg.text_payload = malloc(strlen(text) + 1);
    if (msg.text_payload == NULL) {
        ESP_LOGE("TG_QUEUE", "Брак пам'яті для виділення тексту в чергу!");
        return;
    }

    // 3. Копіюємо текст у виділену пам'ять
    strcpy(msg.text_payload, text);

    // 4. Відправляємо в чергу. 
    // Якщо черга забита, чекаємо максимум 100 мілісекунд (pdMS_TO_TICKS(100))
    if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE("TG_QUEUE", "Помилка: Черга переповнена! Повідомлення втрачено.");
        free(msg.text_payload); // Обов'язково чистимо пам'ять, якщо черга не прийняла
    } else {
        ESP_LOGI("TG_QUEUE", "Повідомлення успішно додано в чергу.");
    }
}

void photo_QueueSend(void) {
    
    if (telegram_queue == NULL) {
        ESP_LOGE("TG_QUEUE", "Помилка: Черга Telegram ще не створена!");
        return;
    }

    // 1. Створюємо об'єкт повідомлення для черги
    telegram_queue_msg_t msg;
    msg.type = TG_TYPE_PHOTO;
    msg.text_payload = NULL;
    msg.value = 0; // Не використовується для для фото

    // ВІДПРАВЛЯЄМО В ЧЕРГИ обнулюємо усю чергу, бо фото може бути лише од  під час спрацювання сенсора
   //xQueueReset(telegram_queue);
    // Навсяк випадок

    if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE("Photo_QueueSend", "Черга повна..."); 
    }else{

        ESP_LOGE("Photo_QueueSend","Фото відправлено в чергу! Чекайте...");
    } 
}

void аction_QueueSend(tg_msg_type_t msg_type, uint8_t action_value) {
    
    if (action_queue == NULL) {
        ESP_LOGE("TG_QUEUE", "Помилка: Черга Telegram ще не створена!");
        return;
    }

    // 1. Створюємо об'єкт повідомлення для черги
    telegram_queue_msg_t msg;
    msg.type = msg_type;
    msg.text_payload = NULL;
    msg.value = action_value; 

    // ВІДПРАВЛЯЄМО В ЧЕРГИ обнулюємо усю чергу, бо фото може бути лише од  під час спрацювання сенсора
    //xQueueReset(telegram_queue);
    // Навсяк випадок 
    if (xQueueSend(action_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGE("Action_QueueSend", "Черга повна..."); 
    }else{

        ESP_LOGE("Action_QueueSend","Фото відправлено в чергу! Чекайте...");
    }
}

extern uint8_t alarm_val; // Компілятор знайде її в ...main.c

/**
 * @brief Функція парсингу JSON-відповіді від Telegram
 * @param json_str JSON-рядок, отриманий від Telegram   
 * Виконуємо обробку повідомлення та відправляємо данні в чергу telegram_queue для подальшої обробки.
 */
static const char *TAG_PARSE = "PARSE_TELEGRAM";
void parse_telegram_updates(const char* json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    uint8_t current_val = 0;

    cJSON *ok = cJSON_GetObjectItem(root, "ok");
    if (cJSON_IsTrue(ok)) {
        cJSON *result = cJSON_GetObjectItem(root, "result");
        int size = cJSON_GetArraySize(result);
        
        for (int i = 0; i < size; i++) {
            cJSON *item = cJSON_GetArrayItem(result, i);
            cJSON *update_id = cJSON_GetObjectItem(item, "update_id");
            
            // Оновлюємо ID останнього повідомлення
            if (update_id) {
                // Зчитуємо через valuedouble для уникнення переповнення 32-біт
                last_update_id = (int32_t)update_id->valuedouble; 
                ESP_LOGI(TAG_PARSE, "DEBUG: Справжній update_id з JSON = %d\n", last_update_id);
            }

            cJSON *message = cJSON_GetObjectItem(item, "message");
            if (message) {
                cJSON *chat = cJSON_GetObjectItem(message, "chat");
                cJSON *chat_id_obj = cJSON_GetObjectItem(chat, "id");
                cJSON *text = cJSON_GetObjectItem(message, "text");

                if (text && chat_id_obj) {
                    char chat_id_str[32];
                    snprintf(chat_id_str, sizeof(chat_id_str), "%lld", (long long)chat_id_obj->valuedouble);
                    
                    // Перевірка Chat ID на безпеку
                    if (strcmp(chat_id_str, CHAT_ID) == 0) {
                        //ESP_LOGI(TAG_PARSE, "Отримано команду: %s", text->valuestring);
                        
                        // Логіка обробки команд
                        if (strcmp(text->valuestring, "/photo") == 0) {                             //Для відправки фото в телеграм
                            photo_QueueSend();
                            ESP_LOGE(TAG_PARSE,"Фото відправлено в чергу! Чекайте...");
                        }
                        else if (strcmp(text->valuestring, "/led_on") == 0) {                            //Для вмикання світлодіода
                            ESP_LOGI(TAG_PARSE, "Дія: Ввімкнути світлod іод");
                            // Безпечно кидаємо сповіщення в чергу з іншої таски!
                            telegram_queue_msg_t msg;
                            msg.type = TG_TYPE_LED;
                            msg.text_payload = NULL;
                            msg.value = 1;       // Ввімкнути світлодіод
                            // ВІДПРАВЛЯЄМО В ЧЕРГУ
                            if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
                                ESP_LOGE(TAG_PARSE, "Черга повна, повертаємо буфер камери");
                                // Тут free(msg.text_payload) робити НЕ треба, бо free(NULL) — це безпечна операція, 
                                // але вона просто нічого не робить.                                 
                            }else{
                                ESP_LOGE(TAG_PARSE, "⚠️ УВАГА! Світлodіод в кімнаті ввімкнено!");
                            }
                            
                        }
                        else if (strcmp(text->valuestring, "/led_off") == 0) {                       //Для вимикання світлодіода
                            ESP_LOGI(TAG_PARSE, "Дія: Вимкнути світлодіод");                
                            // Безпечно кидаємо сповіщення в чергу з іншої таски!
                            telegram_queue_msg_t msg;
                            msg.type = TG_TYPE_LED;
                            msg.text_payload = NULL;
                            msg.value = 0;       // Вимкнути світлoдіод
                            // ВІДПРАВЛЯЄМО В ЧЕРГИ
                            if (xQueueSend(telegram_queue, &msg, pdMS_TO_TICKS(10)) != pdTRUE) {
                                ESP_LOGE(TAG_PARSE, "Черга повна, повертаємо буфер камери");  
                                // Тут free(msg.text_payload) робити НЕ треба, бо free(NULL) — це безпечна операція, 
                                // але вона просто нічого не робить.                               
                            }
                            ESP_LOGE(TAG_PARSE, "⚠️ УВАГА! Світлodіод в кімнаті вимкнено!");
                        }
                        else if (strcmp(text->valuestring, "/alarm_on") == 0) {
                            // Зчитуємо значення
                            current_val = get_alarm_val(); 
                            
                            if (current_val == 0) {
                                set_alarm_val(1); // Змінюємо значення
                                ESP_LOGI(TAG_PARSE, "Дія: Увимкнути сігналізацю");
                                text_QueueSend("⚠️ УВАГА! Сігналізація в домі ввимкнена!");
                                start_gpio_interrupt(CONFIG_SENSOR_GPIO);
                            }else{
                                text_QueueSend("😁 Сігналізація вже працює!!!");
                            }
                        }
                        else if (strcmp(text->valuestring, "/alarm_off") == 0) {
                            // Зчитуємо значення
                            current_val = get_alarm_val();                             
                            if (current_val == 1) {
                                set_alarm_val(0); // Змінюємо значення
                                ESP_LOGI(TAG_PARSE, "Дія: Увимкнути сігналізацю");
                                text_QueueSend("⚠️ УВАГА! Сігналізація в домі ввимкнена!");
                                stop_gpio_interrupt(CONFIG_SENSOR_GPIO);
                            }else{
                                text_QueueSend("😁 Сігналізація вже не працює!!!");
                            }
                        }
                        else if (strcmp(text->valuestring, "/update") == 0) {// Перевіряємо, чи прийшла команда на оновлення
                            // 1. Одразу надсилаємо підтвердження старту перевірки
                            text_QueueSend("Отримано команду на апгрейд. Перевіряю GitHub... 🔄");
                            
                            // 2. Викликаємо модифіковану функцію
                            bool is_latest = check_and_run_ota();
                            
                            // 3. Аналізуємо результат
                            if (is_latest) {
                                // Якщо повернулося true — плата не шилася, сповіщаємо користувача
                                text_QueueSend("У вас вже встановлена найновіша версія прошивки! ✅");
                            } else {
                                // Якщо повернулося false і плата НЕ перезавантажилась — сталася помилка (наприклад, збій інтернету)
                                text_QueueSend("Не вдалося виконати перевірку або оновлення. Перевірте логі чи підключення! ❌");
                            }
                        }
                        else{
                            ESP_LOGW(TAG_PARSE, "Нерозпізнаний команд: %s", text->valuestring);
                            // Безпечно кидаємо сповіщення в чергу з іншої таски!
                            text_QueueSend(text->valuestring);
                        }
                    }
                }
            }
        }
    }
    cJSON_Delete(root); 
}


/**
 * @brief Функція для відправки повідомлення в Telegram
 * @param text Текст повідомлення
 */

void send_telegram_message(const char* text) {
    if (text == NULL) return;

    // Перевіряємо, чи клієнт ініціалізований
    if (global_tg_client == NULL) {
        if (!init_telegram_client()) return;
    }

    static char json_payload[512];
    snprintf(json_payload, sizeof(json_payload), "{\"chat_id\": \"%s\", \"text\": \"%s\"}", CHAT_ID, text);

    // Створюємо структуру відповіді (таку ж, як в тасці getUpdates)
    http_response_t my_response = { .buffer = NULL, .buffer_len = 0 };
    
    // Оновлюємо налаштування клієнта для поточного запиту
    esp_http_client_set_url(global_tg_client, _URL_POST_MESSAGE);
    esp_http_client_set_method(global_tg_client, HTTP_METHOD_POST);
    
    esp_http_client_set_user_data(global_tg_client, &my_response); // Передаємо структуру для обробника подій
    
    esp_http_client_set_header(global_tg_client, "Content-Type", "application/json");
    esp_http_client_set_post_field(global_tg_client, json_payload, strlen(json_payload));
    
    // Виконуємо запит (Event Handler сам викачає відповідь повністю в my_response.buffer)
    esp_err_t err = esp_http_client_perform(global_tg_client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(global_tg_client);
        ESP_LOGI("TG", "HTTP Status Code: %d", status_code);
        
        if (status_code == 200) {
            ESP_LOGI("TG", "Повідомлення успішно надіслано!");
        } else {
            ESP_LOGE("TG", "Telegram повернув помилку: %d", status_code);
            
            // Якщо сталася помилка (код 400), текст помилки ВЖЕ зібраний у нашому буфері!
            if (my_response.buffer != NULL) {
                ESP_LOGW("TG", "Опис помилки від Telegram: %s", my_response.buffer);
            } else {
                ESP_LOGW("TG", "Помилка відповіді, буфер порожній.");
            }
        }
    } else {
        ESP_LOGE("TG", "Помилка відправки тексту: %s", esp_err_to_name(err));
        // Закриваємо сесію, щоб очистити зламаний сокет
        esp_http_client_cleanup(global_tg_client);
        global_tg_client = NULL; 
    }
    
    // --- ОЧИЩЕННЯ РЕСУРСІВ (Обов'язково!) ---
    if (my_response.buffer != NULL) {
        free(my_response.buffer);
        my_response.buffer = NULL; // Занулюємо після очищення
        my_response.buffer_len = 0;
    }
    
    /* if (global_tg_client != NULL) {
        esp_http_client_cleanup(global_tg_client);
    } */
}
/**
 * @brief Функція для відправки фото в Telegram
 */


/* 04082026 1517*/
bool send_telegram_photo(const uint8_t *buf, size_t len) {
    bool is_success = false;
    ESP_LOGE("TG", "Нsend_telegram_photo для відправки кадру!");
    if (buf == NULL || len == 0) return is_success;
    
    // Перевіряємо, чи клієнт ініціалізований
    if (global_tg_client == NULL) {
        if (!init_telegram_client()) return is_success;
    }
    ESP_LOGE("TG", "Пройшли перевірку global_tg_client!");
    const char *boundary = "----ESP32Boundary12345";
    
    // Формуємо заголовки
    char header_ct[128];
    snprintf(header_ct, sizeof(header_ct), "multipart/form-data; boundary=%s", boundary);
    
    char body_start[384]; 
    snprintf(body_start, sizeof(body_start), 
             "--%s\r\nContent-Disposition: form-data; name=\"chat_id\"\r\n\r\n" CHAT_ID "\r\n"
             "--%s\r\nContent-Disposition: form-data; name=\"photo\"; filename=\"cam_02.jpg\"\r\n"
             "Content-Type: image/jpeg\r\n\r\n", boundary, boundary);
             
    char body_end[64];
    snprintf(body_end, sizeof(body_end), "\r\n--%s--\r\n", boundary);
    
    size_t body_start_len = strlen(body_start);
    size_t body_end_len = strlen(body_end);
    size_t total_length = body_start_len + len + body_end_len;

    ESP_LOGE("TG", "Виділяємо один великий буфер у SPIRAM під весь пакет!");

    // Виділяємо один великий буфер у SPIRAM під весь пакет
    char *full_body = heap_caps_malloc(total_length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (full_body == NULL) {
        ESP_LOGE("TG", "Не вдалося виділити пам'ять у SPIRAM для кадру!");
        return false;
    }
    
    // Склеюємо все в один монолітний шматок пам'яті
    memcpy(full_body, body_start, body_start_len);
    memcpy(full_body + body_start_len, buf, len);
    memcpy(full_body + body_start_len + len, body_end, body_end_len);
    
    // Оновлюємо налаштування клієнта для поточного запиту
    esp_http_client_set_url(global_tg_client, _URL_POST_PHOTO);
    esp_http_client_set_method(global_tg_client, HTTP_METHOD_POST);
    esp_http_client_set_header(global_tg_client, "Content-Type", header_ct);
    
    // Створюємо структуру відповіді (таку ж, як в тасці getUpdates)
    http_response_t my_response = { .buffer = NULL, .buffer_len = 0 };
    esp_http_client_set_user_data(global_tg_client, &my_response); // Передаємо структуру для обробника подій

    // Передаємо дані: ESP-IDF v6 сам виставить потрібний Content-Length
    esp_http_client_set_post_field(global_tg_client, full_body, total_length);
    
    // Відправляємо все ОДНИМ TCP/TLS пакетом
    
    esp_err_t err = esp_http_client_perform(global_tg_client);
    
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(global_tg_client);
        
        // Обов'язково вичитуємо відповідь до кінця, щоб зберегти з'єднання Keep-Alive!
        char flush_buf[256];
        while (!esp_http_client_is_complete_data_received(global_tg_client)) {
            int ret = esp_http_client_read(global_tg_client, flush_buf, sizeof(flush_buf));
            if (ret <= 0) break;
        }

        if (status_code == 200) {
            ESP_LOGI("TG", "Фото успішно надіслано! Код 200");
            is_success = true;
        } else {
            ESP_LOGE("TG", "Telegram повернув помилку: %d", status_code);
        }
    } else {
        ESP_LOGE("TG", "Помилка виконання HTTP запиту: %s", esp_err_to_name(err));
        // Закриваємо сесію, щоб очистити зламаний сокет
        esp_http_client_cleanup(global_tg_client);
        global_tg_client = NULL; 
    }
    
    // ОЧИЩЕННЯ: Звільняємо пам'ять буфера, але КЛІЄНТА НЕ ВИДАЛЯЄМО!
    heap_caps_free(full_body);
    //Завжди чистимо виділений під JSON буфер
    if (my_response.buffer != NULL) {
        free(my_response.buffer);
        my_response.buffer = NULL; // Занулюємо після очищення
        my_response.buffer_len = 0;
    }
    return is_success;
}

/** @brief Task для обробки повідомлень Telegram
 *  @param pvParameters Параметри задачі
 * Фонова задача-менеджер TelegramВона спить і прокидається лише тоді, 
 * коли в чергу щось падає. Одночасність виключена на рівні архітектур
 */
static const char *TAG_TG = "Queue_task";
void telegram_queue_task(void *pvParameters) {
    telegram_queue_msg_t received_msg;
    
    // Намагаємося захопити м'ютекс перед КОЖНИМ запитом getUpdates
    if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
        send_telegram_message(text_start);
        // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
        // щоб таска з фото могла його перехопити
        xSemaphoreGive(tg_http_mutex);
    }
    // Створєно чергу на 5 повідомлень
    //telegram_queue = xQueueCreate(5, sizeof(telegram_queue_msg_t));

    while (1) {
        // Чекаємо повідомлення з черги (блокуючий виклик, не їсть процесор)
        if (xQueueReceive(telegram_queue, &received_msg, portMAX_DELAY) == pdTRUE) {
            
            if (received_msg.type == TG_TYPE_TEXT) {
                ESP_LOGI(TAG_TG, "Відправка тексту...");
                // Намагаємося захопити м'ютекс перед КОЖНИМ запитом getUpdates
                if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
                    send_telegram_message(received_msg.text_payload);
                    // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
                    // щоб таска з фото могла його перехопити
                    xSemaphoreGive(tg_http_mutex);
                }

                free(received_msg.text_payload); // Звільняємо пам'ять тексту після відправки!
            } 
            else if (received_msg.type == TG_TYPE_PHOTO) {
                // Робимо фотку
                // 1. Створюємо чистий локальний вказівник для цього конкретного кадру
                // Намагаємося захопити м'ютекс перед КОЖНИМ запитом getUpdates
                if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
#ifdef Camera                 
                    camera_fb_t *fb = NULL;

                    // 2. Робимо фотку прямо в локальний вказівник
                    // Передаємо адресу вказівника через оператор &
                    esp_err_t err = get_camera_capture(&fb);
                    if (err == ESP_OK) {
                        // Обробка кадру (наприклад, відправка по HTTP або збереження)
                        // Доступ до даних: my_frame->buf, my_frame->len
                        // Відправляємо фото в Telegram
                        ESP_LOGI(TAG_TG, "Відправка фото...");

                        bool success = send_telegram_photo(fb->buf, fb->len);

                        if (!success) {
                            // Мережа зняла збій, можна спробувати ще раз 
                            // або увімкнути червоний світлодіод помилки
                            ESP_LOGE(TAG_TG, "Фото НЕ відправлено! Помилка мережі.");
                        } else{
                            ESP_LOGI(TAG_TG, "Фото успішно відправлено!");
                        }
                        esp_camera_fb_return(fb); // Цього ОДНОГО виклику абсолютно достатньо!
                        //БЕЗПЕКА: обнуляємо вказівник, щоб уникнути "диких вказівників" (dangling pointers)
                        fb = NULL; 
                        ESP_LOGI(TAG_TG, "Буфер камери успішно звільнено.");
                    } else {
                        ESP_LOGE(TAG_TG, "Помилка зйомки фото: %d", err);  
                    }
                     // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
                    // щоб таска з фото могла його перехопити
                    xSemaphoreGive(tg_http_mutex);
                }
#endif
            }
            if (received_msg.type == TG_TYPE_LED) {
                ESP_LOGI(TAG_TG, "Працюємо зі світлodіодом...");
                if (received_msg.value == 1) {
                    // Увімкнути світлодіод
                    gpio_set_level(BLINK_GPIO, 1);
                    ESP_LOGI(TAG_TG, "gpio_set_level in 1");
                    if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
                        send_telegram_message("⚠️ УВАГА! Світлодіод в кімнаті ввімкнено!");
                        // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
                        // щоб таска з фото могла його перехопити
                        xSemaphoreGive(tg_http_mutex);
                    }
                } else {
                    // Вимкнути світлодіод
                    gpio_set_level(BLINK_GPIO, 0);
                    ESP_LOGI(TAG_TG, "gpio_set_level in 0");
                    if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
                        send_telegram_message("⚠️ УВАГА! Світлodіod в кімнаті вимкнено!");
                        // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
                        // щоб таска з фото могла його перехопити
                        xSemaphoreGive(tg_http_mutex);
                    }
                }
                {
                    /* code */
                }
                
                // Намагаємося захопити м'ютекс перед КОЖНИМ запитом getUpdates
                if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
                    send_telegram_message(received_msg.text_payload);
                    // Обов'язково ВІДПУСКАЄМО м'ютекс одразу після завершення запиту,
                    // щоб таска з фото могла його перехопити
                    xSemaphoreGive(tg_http_mutex);
                }
                free(received_msg.text_payload); // Звільняємо пам'ять тексту після відправки!
            } 
        }
    }
}

// Task для періодичного опитування (Long Polling)
void telegram_bot_task(void *pvParameters) {

    while(1) {
        static char url[256];
        
        // Якщо активовано режим оновлення — таска просто спить і не чіпає мережу
        if (telegram_bot_paused) {
            vTaskDelay(pdMS_TO_TICKS(5000)); // Спимо 5 секунд і перевіряємо знову
            continue;
        }

        
        // Захоплюємо м'ютекс перед початком БУДЬ-ЯКИХ дій з мережею
        if (xSemaphoreTake(tg_http_mutex, portMAX_DELAY) == pdTRUE) {
            // Перевіряємо, чи клієнт ініціалізований
            if (global_tg_client == NULL) {
                if (!init_telegram_client()) {
                    ESP_LOGW("TG_POLL", "Спроба переініціалізації HTTP-клієнта провалилась. Виходимо з блоку мережі.");
                    xSemaphoreGive(tg_http_mutex); // ОБОВ'ЯЗКОВО ВІДПУСКАЄМО М'ЮТЕКС ПЕРЕД КРОКОМ НАЗАД!
                    vTaskDelay(pdMS_TO_TICKS(5000)); // Чекаємо ззовні м'ютексу
                    continue; // Переходимо на наступну ітерацію циклу while(1)
                }
            }
            
            http_response_t my_response = { .buffer = NULL, .buffer_len = 0 };
            esp_http_client_set_user_data(global_tg_client, &my_response); // Передаємо структуру для обробника подій

            // Формуємо актуальний URL
            memset(url, 0, sizeof(url));
            snprintf(url, sizeof(url), _URL_OFFSET, last_update_id + 1);
            ESP_LOGW(TAG, "URL...%s", url);
            esp_http_client_set_url(global_tg_client, url);

            // Виконуємо запит
            esp_err_t err = esp_http_client_perform(global_tg_client);
            
            if (err == ESP_OK) {
                int status = esp_http_client_get_status_code(global_tg_client);
                if (status == 200) {
                    if (my_response.buffer != NULL) {
                        ESP_LOGI(TAG, "Дані отримано. Запуск парсингу...");  
                        // Виводимо вміст буфера в консоль з тегом вашої таски
                        ESP_LOGI(TAG, "JSON від Telegram:\n%s", my_response.buffer);                   
                        parse_telegram_updates(my_response.buffer); 
                        ESP_LOGI(TAG, "Парсинг завершено. Оновлений last_update_id = %d", last_update_id);
                    }
                } else if (status == 400) {
                    char response_buffer[256];
                    int read_len = esp_http_client_read_response(global_tg_client, response_buffer, sizeof(response_buffer) - 1);
                    if (read_len > 0) {
                        response_buffer[read_len] = '\0'; // Гарантуємо кінець рядка
                        ESP_LOGE("TG", "Опис помилки 400: %s", response_buffer);
                    } 
                } else {
                            ESP_LOGW(TAG, "Telegram повернув статус-код: %d", status);
                        }
            }else if (err == ESP_ERR_HTTP_EAGAIN) {
                        // Це норма для Long Polling! Повідомлень немає.
                        ESP_LOGI(TAG, "Нових повідомлень немає (таймаут). Повторюємо запит...");
                } else {
                    ESP_LOGE(TAG, "HTTP запит завалився: %s. Скидаємо клієнт.", esp_err_to_name(err));
                    // Закриваємо сесію, щоб очистити зламаний сокет
                    esp_http_client_cleanup(global_tg_client);
                    global_tg_client = NULL; 
                }

            //Завжди чистимо виділений під JSON буфер але кліент залишаємо живим, бо він буде використовуватися для наступного запиту
            if (my_response.buffer != NULL) {
                free(my_response.buffer);
            }
            // НАЙВАЖЛИВІШЕ: М'ютекс звільняється тільки після повного очищення
            xSemaphoreGive(tg_http_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(2000)); // Звичайна пауза між опитуваннями (мережа вільна)
    }
    if (global_tg_client != NULL) {
            esp_http_client_cleanup(global_tg_client);
        }
    vTaskDelete(NULL);
}

