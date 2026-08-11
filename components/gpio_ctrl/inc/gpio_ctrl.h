#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t gpio_ctrl_init_pin(gpio_num_t pin, gpio_mode_t mode, gpio_pull_mode_t pull_up, gpio_pull_mode_t pull_down);
esp_err_t gpio_ctrl_set_level(gpio_num_t pin, uint32_t level);
esp_err_t gpio_ctrl_get_level(gpio_num_t pin, int *level);

void init_gpio_interrupt(void);   

#ifdef __cplusplus
}
#endif
