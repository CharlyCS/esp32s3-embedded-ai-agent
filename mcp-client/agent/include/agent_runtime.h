#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t agent_runtime_init(void);

/* Callback registered in the AWS IoT transport component. */
esp_err_t agent_runtime_submit_task(
    const char *payload,
    size_t payload_len
);

#ifdef __cplusplus
}
#endif
