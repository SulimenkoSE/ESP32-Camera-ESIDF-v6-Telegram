#pragma once

#include <stdio.h>
#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C"
{
#endif

// Ініціалізує SNTP, чекає 30 сек і запускає фоновий повтор у разі невдачі
void time_utils_init_with_timeout(void);
void start_time_sync(void); 

bool get_current_time_string(char *buffer, size_t max_len);
bool get_current_time_struct(struct tm *target_time_struct);
void check_night_mode(void);

#ifdef __cplusplus
}
#endif
