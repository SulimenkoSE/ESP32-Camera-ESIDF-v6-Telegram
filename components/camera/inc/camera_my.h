#pragma once

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Структура для зберігання кадрів у PSRAM
    typedef struct {
        uint8_t* buf;
        size_t len;
    } stored_frame_t;

    esp_err_t camera_init(void);
    //void take_photo_and_send_to_telegram(void);
    esp_err_t get_camera_capture(camera_fb_t **fb);

    // Оголошення функцій, які будуть доступні між файлами
    void record_mjpeg_to_ram(void);
    void clear_video_buffer(void);

    // Функції-гетери для отримання доступу до даних в іншому файлі
    stored_frame_t* get_video_buffer(void);
    int get_video_frame_count(void);

    void clear_camera_buffer(camera_fb_t **fb);
#ifdef __cplusplus
}
#endif