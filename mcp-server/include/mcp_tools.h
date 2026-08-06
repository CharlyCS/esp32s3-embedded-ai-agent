#pragma once

#include "esp_err.h"
#include "esp_mcp_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcp_tools_register_all(esp_mcp_t *mcp);

#ifdef __cplusplus
}
#endif
