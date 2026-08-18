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
#define MAX_VIDEO_SIZE (3 * 1024 * 1024) // 3 МБ під суцільний блок

static const char *TAG = "Camera";

// Глобальні змінні модуля
static uint8_t *video_buffer = NULL;
static stored_frame_t frame_meta[MAX_FRAMES];
static int frame_count = 0;
static uint32_t current_offset = 0;

// Хендл для захисту камери
SemaphoreHandle_t camera_mutex = NULL;

// Ініціалізація: викликається один раз при старті системи
void init_video_buffer(void) {
    video_buffer = (uint8_t *)heap_caps_malloc(MAX_VIDEO_SIZE, MALLOC_CAP_SPIRAM);
    if (video_buffer == NULL) {
        ESP_LOGE(TAG, "КРИТИЧНО: Не вдалося виділити 3MB у PSRAM!");
        return;
    }
    memset(frame_meta, 0, sizeof(frame_meta));
    ESP_LOGI(TAG, "Буфер 3MB у PSRAM успішно ініціалізовано.");
}



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
    
    // Ініціалізація: викликається один раз при старті системи
    init_video_buffer();
    
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
    //Поворот зображення
    sensor_t *s = esp_camera_sensor_get();
    if (s != NULL) {
        s->set_vflip(s, 1);
        s->set_hmirror(s, 0);
    }

    
    return ESP_OK;
}

// Очищення: просто скидає покажчики, ЖОДНОГО free() чи фрагментації!
void clear_video_buffer(void) {
    frame_count = 0;
    current_offset = 0;
    // Оскільки ми не звільняємо пам'ять, memset робити не обов'язково (економить CPU)
    ESP_LOGI(TAG, "Буфер відео скинуто у початковий стан.");
}

// Гетери для використання в інших файлах
int get_video_frame_count(void) {
    return frame_count;
}

// Повертає вказівник на кадр за індексом та записує його довжину
uint8_t* get_video_frame(int index, uint32_t *out_len) {
    if (index < 0 || index >= frame_count || out_len == NULL) {
        return NULL;
    }
    *out_len = frame_meta[index].len;
    return frame_meta[index].buf;
}

// 1. ЗАПИС ВІДЕО
void record_mjpeg_to_ram(void) {
    if (camera_mutex == NULL || video_buffer == NULL) return;

    if (xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Таймаут відео: камера зайнята!");
        return;
    }

    clear_video_buffer(); 
    ESP_LOGI(TAG, "Початок запису відео...");    

    while (frame_count < MAX_FRAMES) {
        camera_fb_t * fb = esp_camera_fb_get();
        if (!fb) {
            ESP_LOGE(TAG, "Помилка захоплення кадру відео");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // Перевіряємо, чи поміститься кадр у залишок буфера
        if (current_offset + fb->len > MAX_VIDEO_SIZE) {
            ESP_LOGW(TAG, "Буфер заповнено раніше ліміту кадрів (%d байт залишилось)", MAX_VIDEO_SIZE - current_offset);
            esp_camera_fb_return(fb);
            break;
        }

        // Копіюємо прямо у виділений блок PSRAM без malloc!
        memcpy(&video_buffer[current_offset], fb->buf, fb->len);

        // Запам'ятовуємо метадані кадру
        frame_meta[frame_count].buf = &video_buffer[current_offset];
        frame_meta[frame_count].len = fb->len;

        current_offset += fb->len;
        frame_count++;
        
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(120)); // ~8 FPS
    }
    
    xSemaphoreGive(camera_mutex);
    ESP_LOGI(TAG, "Запис відео завершено. Кадрів: %d, Використано пам'яті: %d КБ", frame_count, current_offset / 1024);
}

// 2. ЗЙОМКА ФОТО (Ваш початковий запит)
bool take_photo_to_ram(void) {
    if (camera_mutex == NULL || video_buffer == NULL) return false;

    if (xSemaphoreTake(camera_mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Таймаут фото: камера зайнята!");
        return false;
    }

    clear_video_buffer(); // Скидаємо індекси на 0
    ESP_LOGI(TAG, "Зйомка фото у буфер...");

    camera_fb_t * fb = esp_camera_fb_get();
    if (!fb) {
        ESP_LOGE(TAG, "Помилка захоплення кадру фото");
        xSemaphoreGive(camera_mutex);
        return false;
    }

    if (fb->len > MAX_VIDEO_SIZE) {
        ESP_LOGE(TAG, "Критично: Розмір фото (%d) перевищує весь буфер (%d)!", fb->len, MAX_VIDEO_SIZE);
        esp_camera_fb_return(fb);
        xSemaphoreGive(camera_mutex);
        return false;
    }

    // Фото завжди лягає в початок буфера (offset = 0)
    memcpy(&video_buffer[0], fb->buf, fb->len);

    frame_meta[0].buf = &video_buffer[0];
    frame_meta[0].len = fb->len;
    
    frame_count = 1; // Маркер того, що в нас лежить 1 фото, а не відео
    current_offset = fb->len;

    esp_camera_fb_return(fb);
    xSemaphoreGive(camera_mutex);
    ESP_LOGI(TAG, "Фото збережено. Розмір: %d байт", fb->len);
    return true;
}

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