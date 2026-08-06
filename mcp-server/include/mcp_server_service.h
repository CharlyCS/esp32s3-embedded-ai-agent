#pragma once

#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t mcp_server_service_init(void);

/*
 * Fast in-process transport used by the MCP client on the same ESP32.
 * The caller owns *out_response_json and must free() it.
 * Notifications can legitimately return *out_response_json == NULL.
 */
esp_err_t mcp_server_process_local_json(
    const char *request_json,
    char **out_response_json
);

/* Optional LAN endpoint: http://ESP32_IP/mcp */
esp_err_t mcp_server_start_http(void);

#ifdef __cplusplus
}
#endif
