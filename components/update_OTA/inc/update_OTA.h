#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

bool  check_and_run_ota(void);
void validate_new_firmware(void);

#ifdef __cplusplus
}
#endif


