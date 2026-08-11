#pragma once

#include "esp_err.h"
#include "esp_camera.h"

#ifdef __cplusplus
extern "C"
{
#endif

    esp_err_t camera_init(void);
    //void take_photo_and_send_to_telegram(void);
    esp_err_t get_camera_capture(camera_fb_t **fb);
#ifdef __cplusplus
}
#endif