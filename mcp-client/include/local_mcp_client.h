#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t local_mcp_client_init(void);

esp_err_t local_mcp_client_list_tools(char **out_response_json);
esp_err_t local_mcp_client_list_resources(char **out_response_json);
esp_err_t local_mcp_client_read_resource(
    const char *uri,
    char **out_response_json
);
esp_err_t local_mcp_client_list_prompts(char **out_response_json);
esp_err_t local_mcp_client_get_prompt(
    const char *name,
    const char *arguments_json,
    char **out_response_json
);
esp_err_t local_mcp_client_call_tool(
    const char *tool_name,
    const char *arguments_json,
    char **out_response_json
);

#ifdef __cplusplus
}
#endif
