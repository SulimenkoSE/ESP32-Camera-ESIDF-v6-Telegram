#ifndef MAIN_H
#define MAIN_H

#include <stdint.h>
#define BLINK_GPIO CONFIG_BLINK_GPIO

uint8_t get_alarm_val(void);
void set_alarm_val(uint8_t new_val);

#endif