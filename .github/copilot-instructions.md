# ESP-IDF v6 Project Rules
- We are strictly using ESP-IDF version 6.0+ (v6).
- All legacy peripheral drivers (old ADC, DAC, I2S, RMT, MCPWM, legacy Timer Group) are completely removed in v6. You must ONLY use the new Driver APIs (e.g., GPTimer, ADC Oneshot/Continuous, New UART driver).
- Do not assume FreeRTOS headers are implicitly included in peripheral headers. Always explicitly include `#include "freertos/FreeRTOS.h"` and `#include "freertos/task.h"`.
- Use Picolibc standards where applicable.
- Always check error return codes using `esp_err_t` and `ESP_ERROR_CHECK()`.
- If you are unsure about an API in version 6, strictly state that you don't know rather than hallucinating old v4 or v5 functions.
