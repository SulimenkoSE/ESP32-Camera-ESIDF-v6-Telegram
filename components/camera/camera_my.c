#include "camera_my.h"

#include "esp_camera.h"
#include "esp_http_client.h"
#include "driver/gpio.h"
#include "freertos/event_groups.h"
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_tls.h"
#include "telegram.h"

// WROVER-KIT PIN Map
// #define CAMERA_MODEL_AI_THINKER
#define CAM_PIN_PWDN 32
#define CAM_PIN_RESET -1 // software reset will be performed
#define CAM_PIN_XCLK 0
#define CAM_PIN_SIOD 26
#define CAM_PIN_SIOC 27

#define CAM_PIN_D7 35
#define CAM_PIN_D6 34
#define CAM_PIN_D5 39
#define CAM_PIN_D4 36
#define CAM_PIN_D3 21
#define CAM_PIN_D2 19
#define CAM_PIN_D1 18
#define CAM_PIN_D0 5
#define CAM_PIN_VSYNC 25
#define CAM_PIN_HREF 23
#define CAM_PIN_PCLK 22

static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    .xclk_freq_hz = 20000000, // Частота XCLK (20MHz)
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,

    .pixel_format = PIXFORMAT_JPEG, // Формат для збереження/передачі YUV422,GRAYSCALE,RGB565,JPEG

#if CONFIG_FRAMESIZE_VGA
    .frame_size = FRAMESIZE_VGA,
#define FRAMESIZE_STRING "640x480"
#elif CONFIG_FRAMESIZE_SVGA
    .frame_size = FRAMESIZE_SVGA,
#define FRAMESIZE_STRING "800x600"
#elif CONFIG_FRAMESIZE_XGA
    .frame_size = FRAMESIZE_XGA,
#define FRAMESIZE_STRING "1024x768"
#elif CONFIG_FRAMESIZE_HD
    .frame_size = FRAMESIZE_HD,
#define FRAMESIZE_STRING "1280x720"
#elif CONFIG_FRAMESIZE_SXGA
    .frame_size = FRAMESIZE_SXGA,
#define FRAMESIZE_STRING "1280x1024"
#elif CONFIG_FRAMESIZE_UXGA
    .frame_size = FRAMESIZE_UXGA,
#define FRAMESIZE_STRING "1600x1200"
#endif

    //.frame_size = FRAMESIZE_QVGA,   // роздільна здатність QQVGA-UXGA, For ESP32, do not use sizes above QVGA when not JPEG. The performance of the ESP32-S series has improved a lot, but JPEG mode always gives better frame rates.

    .jpeg_quality = 12,                 // 0-63, for OV series camera sensors, lower number means higher quality 12
    .fb_count = 2,                      // Кількість буферів кадру в PSRAM (2 дозволяє робити double-buffering) When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode.
    .fb_location = CAMERA_FB_IN_PSRAM,  // КРИТИЧНО: Примусово розміщуємо frame buffer в PSRAM
    //.grab_mode = CAMERA_GRAB_WHEN_EMPTY // CAMERA_GRAB_LATEST. Sets when buffers should be filled
    .grab_mode = CAMERA_GRAB_LATEST //Режим CAMERA_GRAB_LATEST каже драйверу: "Коли я прошу кадр, очисти всі внутрішні буфери сам і дай мені виключно останній знімок, який щойно зійшов з матриці".

};

#define MAX_FRAMES 40 // ~4-5 секунд при 8 FPS
static stored_frame_t video_buffer[MAX_FRAMES];
static int frame_count = 0;

// Хендл для захисту камери
SemaphoreHandle_t camera_mutex = NULL;

/* #define MAX_VIDEO_SIZE (3 * 1024 * 1024) // 3 Мегабайти під відео в PSRAM
uint8_t *video_buffer = NULL;
size_t current_video_len = 0;

void init_video_buffer() {
    video_buffer = (uint8_t *)heap_caps_malloc(MAX_VIDEO_SIZE, MALLOC_CAP_SPIRAM);
    if (video_buffer == NULL) {
        ESP_LOGE("BUFF", "Не вдалося виділити пам'ять у PSRAM!");
    }
} */

static const char *TAG = "Camera";

void init_camera_power_pin(void)
{

#if defined(CAM_PIN_PWDN) && CAM_PIN_PWDN >= 0
    gpio_config_t pwdn_conf = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = BIT64(CAM_PIN_PWDN)};
    gpio_config(&pwdn_conf);
    // Встановлюємо початковий стан (наприклад, LOW)
    gpio_set_level(CAM_PIN_PWDN, 0);
#endif
}

esp_err_t camera_init(void)
{
    // Створюємо м'ютекс перед запуском задач камери
    camera_mutex = xSemaphoreCreateMutex();
    if (camera_mutex == NULL) {
        ESP_LOGE(TAG, "Не вдалося створити м'ютекс для камери!");
    }

    // power up the camera if PWDN pin is defined
    init_camera_power_pin();

    // initialize the camera
    esp_err_t err = esp_camera_init(&camera_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Помилка ініціалізації камери: 0x%x", err);
        return err;
    }
    ESP_LOGI(TAG, "Камеру успішно ініціалізовано в PSRAM!");
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }
    return ESP_OK;
}

/* esp_err_t camera_capture(void)
{
    // acquire a frame
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Не вдалося захопити кадр");
        return ESP_FAIL;
    }
    // replace this with your own function
    // process_image(fb->width, fb->height, fb->format, fb->buf, fb->len);
    ESP_LOGI(TAG, "Кадр отримано! Розмір файлу: %zu байт", fb->len);
    // return the frame buffer back to the driver for reuse
    esp_camera_fb_return(fb);
    return ESP_OK;
} */


esp_err_t get_camera_capture(camera_fb_t **fb) {
    if (fb == NULL) {
        ESP_LOGE(TAG, "Передано нульовий вказівник для буфера");
        return ESP_ERR_INVALID_ARG;
    }

    if (camera_mutex == NULL) {
        ESP_LOGE(TAG, "М'ютекс камери не ініціалізовано");
        return ESP_ERR_INVALID_STATE;
    }

    // Чекаємо, поки камера звільниться (максимум 6 секунд)
    if (xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(6000)) != pdTRUE) {
        ESP_LOGE(TAG, "Таймаут: камера зайнята іншою задачею!");
        return ESP_ERR_TIMEOUT;
    }

    // Захоплення кадру з матриці (тепер це безпечно)
    *fb = esp_camera_fb_get();
    
    if (!(*fb)) {
        ESP_LOGE(TAG, "Не вдалося захопити кадр");
        // Якщо кадр не взяли, ОБО В'ЯЗКОВО відпускаємо м'ютекс відразу
        xSemaphoreGive(camera_mutex);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Кадр успішно отримано. Розмір: %u байт", (*fb)->len);
    return ESP_OK;
}

// Функція безпечного очищення пам'яті відео буфера
void clear_camera_buffer(camera_fb_t **fb) {
    if (fb == NULL || *fb == NULL) {
        ESP_LOGW(TAG, "Буфер кадру вже порожній або NULL");
        return;
    }
    // Передаємо саме вказівник на структуру, а не на вказівник
    esp_camera_fb_return(*fb);
    *fb = NULL; // Безпека: запобігає Use-After-Free

    // ВІДПУСКАЄМО М'ЮТЕКС. Тепер інша задача може викликати get_camera_capture
    if (camera_mutex != NULL) {
        xSemaphoreGive(camera_mutex);
    }
}

/**********************Video********************************************/
// Реалізація гетерів для іншого файлу
stored_frame_t* get_video_buffer(void) {
    record_mjpeg_to_ram();
    return video_buffer;
}

int get_video_frame_count(void) {
    return frame_count;
}

// Функція безпечного очищення пам'яті відео буфера
void clear_video_buffer(void) {
    for (int i = 0; i < frame_count; i++) {
        if (video_buffer[i].buf != NULL) {
            free(video_buffer[i].buf);
            video_buffer[i].buf = NULL;
        }
        video_buffer[i].len = 0;
    }
    frame_count = 0;
    // ОБО В'ЯЗКОВО відпускаємо м'ютекс відразу
    xSemaphoreGive(camera_mutex);
    ESP_LOGI(TAG, "Буфер відео повністю очищено та звільнено.");
}

// 1. Запис 5 секунд відео в пам'ять
void record_mjpeg_to_ram(void) {
    // Спочатку очищаємо старий буфер, якщо він не був очищений
    clear_video_buffer(); 
    frame_count = 0;

    if (camera_mutex == NULL) {
        ESP_LOGE(TAG, "М'ютекс камери не ініціалізовано");
        return;
    }

    // Чекаємо, поки камера звільниться (максимум 5 секунд)
    if (xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Таймаут: камера зайнята іншою задачею!");
        return;
    }
    
    ESP_LOGI(TAG, "Початок запису відео...");    
    while (frame_count < MAX_FRAMES) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Помилка захоплення кадру");
            continue;
        }

        // Виділяємо пам'ять у PSRAM під поточний кадр
        video_buffer[frame_count].buf = (uint8_t*)heap_caps_malloc(fb->len, MALLOC_CAP_SPIRAM);
        if (video_buffer[frame_count].buf) {
            memcpy(video_buffer[frame_count].buf, fb->buf, fb->len);
            video_buffer[frame_count].len = fb->len;
            frame_count++;
        } else {
            ESP_LOGE(TAG, "Брак PSRAM для кадру %d", frame_count);
            esp_camera_fb_return(fb);
            break;
        }
        
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(120)); // Затримка для ~8 FPS
    }
    
    ESP_LOGI(TAG, "Записано кадрів: %d", frame_count);
}
/**********************Video********************************************/

#if CONFIG_REMOTE_IS_VARIABLE_NAME
void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Notification of a time synchronization event");
}

void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    // sntp_setservername(0, "pool.ntp.org");
    ESP_LOGI(TAG, "Your NTP Server is %s", CONFIG_NTP_SERVER);
    sntp_setservername(0, CONFIG_NTP_SERVER);
    sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    sntp_init();
}

esp_err_t obtain_time(void)
{
    initialize_sntp();
    // wait for time to be set
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
    }

    if (retry == retry_count)
        return ESP_FAIL;
    return ESP_OK;
}
#endif // CONFIG_REMOTE_IS_VARIABLE_NAME