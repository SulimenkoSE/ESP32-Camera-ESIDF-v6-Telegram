#pragma once

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Структура для зберігання кадрів у PSRAM
    typedef struct {
        uint8_t* buf;   // Вказівник на початок кадру всередині великого буфера
        size_t len;     // Довжина цього конкретного кадру
    } stored_frame_t;

    esp_err_t camera_init(void);

    // Функції-гетери для отримання доступу до даних в іншому файлі
    uint8_t* get_video_frame(int index, uint32_t *out_len);
    int get_video_frame_count(void);
    void clear_video_buffer(void); 

#ifdef __cplusplus
}
#endif