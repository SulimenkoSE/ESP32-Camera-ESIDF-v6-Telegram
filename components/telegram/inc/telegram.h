#pragma once

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#include "esp_http_client.h"
#include "esp_err.h"

#include "esp_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

// Структура для збереження HTTP-відповіді
typedef struct {
    char *buffer;       // Вказівник на майбутні дані
    int buffer_len;     // Поточна довжина накопичених даних
} http_response_t;

// Тип повідомлення, яке ми хочемо надіслати
typedef enum {
    TG_TYPE_TEXT,
    TG_TYPE_PHOTO,
    TG_TYPE_LED
} tg_msg_type_t;

// Об'єкт повідомлення
typedef struct {
    tg_msg_type_t type;
    char *text_payload;       // Динамічний рядок для тексту
    uint8_t value;           // Значення для TG_TYPE_LED
} telegram_queue_msg_t;

// Сама черга (глобальна)
extern QueueHandle_t telegram_queue; 

//Для включення файлу камери, потрібно розкоментувати наступний рядок. Якщо камера не використовується, залиште його закоментованим.

void telegram_bot_task(void *pvParameters);
void telegram_queue_task(void *pvParameters);
void text_QueueSend(const char *text);
void photo_QueueSend(void);

//Для включення файлу камери, потрібно розкоментувати рядок в головному CMakeList.txt
//add_compile_definitions(Camera)
//Якщо камера не використовується, залиште його закоментованим.

#ifdef __cplusplus
}
#endif