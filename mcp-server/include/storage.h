#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t storage_init(void);
bool storage_file_exists(const char *path);

#ifdef __cplusplus
}
#endif
