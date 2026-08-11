# gpio_ctrl component

Lightweight GPIO helper component for ESP-IDF v6. Initializes GPIOs, sets and reads levels.

Notes:
- If `driver/gpio_filter.h` is available in your SDK, this component will detect it at compile time.
- Current implementation falls back to `gpio_get_level()` for reads; you can extend `src/gpio_ctrl.c` to call gpio_filter APIs if desired.

Usage:
1. Place `components/gpio_ctrl` into your project (already done).
2. In your code include `#include "gpio_ctrl.h"` and call:

```c
ESP_ERROR_CHECK(gpio_ctrl_init_pin(CONFIG_BLINK_GPIO, GPIO_MODE_OUTPUT, GPIO_FLOATING));
ESP_ERROR_CHECK(gpio_ctrl_set_level(CONFIG_BLINK_GPIO, 1));
int level;
ESP_ERROR_CHECK(gpio_ctrl_get_level(CONFIG_BLINK_GPIO, &level));
```
