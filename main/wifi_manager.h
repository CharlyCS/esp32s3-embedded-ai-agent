#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t wifi_manager_connect(void);
const char *wifi_manager_get_ip(void);

#ifdef __cplusplus
}
#endif
