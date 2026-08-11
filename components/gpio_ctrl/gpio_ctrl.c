#include "gpio_ctrl.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "telegram.h" // Для доступу до черги telegram_queue

#if defined(__has_include)
#  if __has_include("driver/gpio_filter.h")
#    include "driver/gpio_filter.h"
#    define GPIO_CTRL_HAVE_FILTER 1
#  else
#    define GPIO_CTRL_HAVE_FILTER 0
#  endif
#else
#  define GPIO_CTRL_HAVE_FILTER 0
#endif

#define INPUT_SIGNAL_GPIO    CONFIG_SENSOR_GPIO
#define DEBOUNCE_TIME_US     50000 // 50 мілісекунд (в мікросекундах) для захисту від брязкіту

static const char *TAG = "gpio_ctrl";
static QueueHandle_t gpio_evt_queue = NULL;
extern QueueHandle_t telegram_queue; 

/**
 * @brief Initialize a GPIO pin.
 * @param pin GPIO pin number
 * @param mode GPIO mode
 * @param pull_up Pull-up mode
 * @param pull_down Pull-down mode
 * @return ESP_OK if successful, otherwise an error code
 */
esp_err_t gpio_ctrl_init_pin(gpio_num_t pin, gpio_mode_t mode, gpio_pull_mode_t pull_up, gpio_pull_mode_t pull_down)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = mode,
        .pull_up_en = (pull_up == GPIO_PULLUP_ONLY) ? 1 : 0,
        .pull_down_en = (pull_down == GPIO_PULLDOWN_ONLY) ? 1 : 0,
        .intr_type = GPIO_INTR_DISABLE,
    };

    ESP_LOGI(TAG, "Configuring pin %d mode %d", pin, mode);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

#if GPIO_CTRL_HAVE_FILTER
    ESP_LOGI(TAG, "gpio_filter.h available — filter support enabled (no runtime config performed)");
    // If you want to enable/ configure filtering, extend here with gpio_filter APIs.
#endif

    return ESP_OK;
}

/**
 * @brief Set the level of a GPIO pin.
 * @param pin GPIO pin number
 * @param level Level to set
 * @return ESP_OK if successful, otherwise an error code
 */
esp_err_t gpio_ctrl_set_level(gpio_num_t pin, uint32_t level)
{
    ESP_LOGD(TAG, "Set pin %d -> %u", pin, level);
    ESP_ERROR_CHECK(gpio_set_level(pin, level));
    return ESP_OK;
}

/**
 * @brief Get the level of a GPIO pin.
 * @param pin GPIO pin number
 * @param level Pointer to store the retrieved level
 * @return ESP_OK if successful, otherwise an error code
 */
esp_err_t gpio_ctrl_get_level(gpio_num_t pin, int *level)
{
    if (level == NULL) {
        ESP_LOGE(TAG, "NULL pointer passed to gpio_ctrl_get_level");
        return ESP_ERR_INVALID_ARG;
    }

#if GPIO_CTRL_HAVE_FILTER
    // Prefer filtered read where available. If the specific gpio_filter API
    // is needed, implement it here — current code falls back to gpio_get_level
    // to remain compatible across ESP-IDF versions.
    ESP_LOGD(TAG, "gpio_filter present, but using gpio_get_level() fallback");
#endif

    *level = gpio_get_level(pin);
    return ESP_OK;
}

// Обробник переривання (ISR) - виконується максимально швидко!

static void IRAM_ATTR gpio_isr_handler(void* arg) {
    uint32_t gpio_num = (uint32_t) arg;
    // Передаємо отримання фото в чергу з ISR
    if (gpio_evt_queue == NULL) {
        return;
    }
    /* // Намагаємося надіслати дані стандартним, дозволеним для всіх черг методом
    if (xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL) == pdFAIL) {
        // Якщо повернувся pdFAIL — черга (розміром 1) зараз заповнена старішим сигналом.
        // Ми вручну витісняємо старі дані (імітуємо Overwrite):
        uint32_t dummy;
        
        // 1. Примусово витягуємо старе значення, звільняючи чергу
        xQueueReceiveFromISR(gpio_evt_queue, &dummy, NULL);
        
        // 2. Записуємо нове, найактуальніше значення GPIO
        xQueueSendFromISR(gpio_evt_queue, &gpio_num, NULL);
    } */
    // ВІДПРАВЛЯЄМО В ЧЕРГИ обнулюємо усю чергу, бо фото може бути лише од  під час спрацювання сенсора
   // Перезапише чергу, якщо вона заповнена а це можливо оскільки в черзі лише один елемент
    xQueueOverwriteFromISR(gpio_evt_queue, &gpio_num, NULL);
}

// Задача, яка чекає на сигнал з черги та обробляє його
static void gpio_task_example(void* arg) {
    uint32_t io_num;
    int last_status = 0;
    int64_t last_interrupt_time = 0;

    while (1) {
        // Чекаємо на подію з черги (блокуючий виклик, не споживає ресурси CPU)
        if (xQueueReceive(gpio_evt_queue, &io_num, portMAX_DELAY)) {
            int64_t current_time = esp_timer_get_time();
            
            // Програмний захист від брязкіту контактів
            if (current_time - last_interrupt_time > DEBOUNCE_TIME_US) {
                // Зчитуємо актуальний стан піна
                // int current_status = gpio_get_level(io_num); //Для С
                int current_status = gpio_get_level((gpio_num_t)io_num); // Для C++                 
                // Перевіряємо, чи статус дійсно змінився на 1 (поява напруги)
                if (current_status == 1 && last_status == 0) {
                    ESP_LOGI(TAG, "Виявлено рух! status = %d", current_status);
                    // ВІДПРАВЛЯЄМО В ЧЕРГИ обнулюємо усю чергу, бо фото може бути лише од  під час спрацювання сенсора
                    xQueueReset(telegram_queue);
                    photo_QueueSend(); // Викликаємо функцію для обробки події (наприклад, робимо фото)
                    // ТУТ ВАШ КОД: наприклад, зробити фото, увімкнути Wi-Fi тощо
                } 
                else if (current_status == 0 && last_status == 1) {
                    ESP_LOGI(TAG, "Подія: Напруга зникла! status = %d", current_status);
                }
                
                last_status = current_status;
                last_interrupt_time = current_time;
                vTaskDelay(pdMS_TO_TICKS(5000)); // Невелика затримка для стабілізації
            }
        }
    }
}

void init_gpio_interrupt(void) {
    // 1. Конфігурація GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << INPUT_SIGNAL_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,       // Стягуємо до GND, коли напруги немає
        .intr_type = GPIO_INTR_ANYEDGE               // Спрацьовувати і на появу (0->1), і на зникнення (1->0)
        //.intr_type = GPIO_INTR_POSEDGE              // Тип преривания GPIO: восходящий фронт (0->1)
    };
    gpio_config(&io_conf);

    // 2. Створення черги для передачі подій (розмір черги - 1 елементів іначке не буде працювати xQueueOverwriteFromISR(gpio_evt_queue, &gpio_num, NULL);)
    gpio_evt_queue = xQueueCreate(1, sizeof(uint32_t));

    // 3. Створення FreeRTOS задачі для обробки подій обов'язково пріорітет 10 вище нізя
    xTaskCreate(gpio_task_example, "gpio_task_example", 3072, NULL, 10, NULL);

    // 4. Встановлення глобального сервісу переривань GPIO
    gpio_install_isr_service(0);

    // 5. Прив'язка нашого обробника до конкретного піна GPIO 13
    gpio_isr_handler_add(INPUT_SIGNAL_GPIO, gpio_isr_handler, (void*) INPUT_SIGNAL_GPIO); 
    
    ESP_LOGI(TAG, "Переривання на GPIO %d успішно налаштовано.", INPUT_SIGNAL_GPIO);
}