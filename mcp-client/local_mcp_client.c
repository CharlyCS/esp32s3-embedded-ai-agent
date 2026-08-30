#include "local_mcp_client.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "mcp_server_service.h"

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "local_mcp_client";

static SemaphoreHandle_t s_client_mutex;
static uint32_t s_request_id = 1;
static bool s_initialized;

static uint32_t next_request_id(void)
{
    return s_request_id++;
}

static esp_err_t validate_response(const char *response_json)
{
    if (response_json == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *response = cJSON_Parse(response_json);
    if (response == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *error = cJSON_GetObjectItem(response, "error");
    const esp_err_t result =
        error == NULL ? ESP_OK : ESP_FAIL;

    if (error != NULL) {
        char *error_text = cJSON_PrintUnformatted(error);
        ESP_LOGE(
            TAG,
            "Error MCP local: %s",
            error_text != NULL ? error_text : "sin detalle"
        );
        free(error_text);
    }

    cJSON_Delete(response);
    return result;
}

static esp_err_t send_raw_request(const char *json,bool expect_response,char **out_response_json)
{
    if (json == NULL || out_response_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_response_json = NULL;

    if (xSemaphoreTake(
            s_client_mutex,
            pdMS_TO_TICKS(30000)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    const esp_err_t err =
        mcp_server_process_local_json(
            json,
            out_response_json
        );

    xSemaphoreGive(s_client_mutex);

    if (err != ESP_OK) {
        return err;
    }

    if (expect_response) {
        return validate_response(*out_response_json);
    }

    return ESP_OK;
}

static esp_err_t request_method(const char *method, cJSON *params,char **out_response_json)
{
    if (method == NULL ||
        params == NULL ||
        out_response_json == NULL) {
        cJSON_Delete(params);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *request = cJSON_CreateObject();
    if (request == NULL) {
        cJSON_Delete(params);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(request, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(
        request,
        "id",
        (double) next_request_id()
    );
    cJSON_AddStringToObject(request, "method", method);
    cJSON_AddItemToObject(request, "params", params);

    char *json = cJSON_PrintUnformatted(request);
    cJSON_Delete(request);

    if (json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err =
        send_raw_request(
            json,
            true,
            out_response_json
        );

    free(json);
    return err;
}

esp_err_t local_mcp_client_init(void)
{
    s_client_mutex = xSemaphoreCreateMutex();
    if (s_client_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON *params = cJSON_CreateObject();
    cJSON *capabilities = cJSON_CreateObject();
    cJSON *client_info = cJSON_CreateObject();

    if (params == NULL ||
        capabilities == NULL ||
        client_info == NULL) {
        cJSON_Delete(params);
        cJSON_Delete(capabilities);
        cJSON_Delete(client_info);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(
        params,
        "protocolVersion",
        "2025-11-25"
    );
    cJSON_AddItemToObject(
        params,
        "capabilities",
        capabilities
    );

    cJSON_AddStringToObject(
        client_info,
        "name",
        "embedded-agent-local-mcp-client"
    );
    cJSON_AddStringToObject(
        client_info,
        "version",
        "1.0.0"
    );
    cJSON_AddItemToObject(
        params,
        "clientInfo",
        client_info
    );

    char *response = NULL;
    esp_err_t err = request_method(
        "initialize",
        params,
        &response
    );

    if (err != ESP_OK) {
        free(response);
        return err;
    }

    ESP_LOGI(TAG, "MCP initialize OK");
    free(response);

    cJSON *notification = cJSON_CreateObject();
    cJSON *notification_params = cJSON_CreateObject();

    if (notification == NULL || notification_params == NULL) {
        cJSON_Delete(notification);
        cJSON_Delete(notification_params);
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(notification, "jsonrpc", "2.0");
    cJSON_AddStringToObject(
        notification,
        "method",
        "notifications/initialized"
    );
    cJSON_AddItemToObject(
        notification,
        "params",
        notification_params
    );

    char *notification_json =
        cJSON_PrintUnformatted(notification);
    cJSON_Delete(notification);

    if (notification_json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    char *ignored_response = NULL;
    err = send_raw_request(
        notification_json,
        false,
        &ignored_response
    );
    free(notification_json);
    free(ignored_response);

    if (err == ESP_OK) {
        s_initialized = true;
        ESP_LOGI(
            TAG,
            "MCP Client local conectado al MCP Server local"
        );
    }

    return err;
}

esp_err_t local_mcp_client_list_tools(
    char **out_response_json
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return request_method(
        "tools/list",
        cJSON_CreateObject(),
        out_response_json
    );
}

esp_err_t local_mcp_client_list_resources(
    char **out_response_json
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return request_method(
        "resources/list",
        cJSON_CreateObject(),
        out_response_json
    );
}

esp_err_t local_mcp_client_read_resource(
    const char *uri,
    char **out_response_json
)
{
    if (!s_initialized || uri == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(params, "uri", uri);
    return request_method(
        "resources/read",
        params,
        out_response_json
    );
}

esp_err_t local_mcp_client_list_prompts(
    char **out_response_json
)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    return request_method(
        "prompts/list",
        cJSON_CreateObject(),
        out_response_json
    );
}

esp_err_t local_mcp_client_get_prompt(const char *name,const char *arguments_json,char **out_response_json)
{
    if (!s_initialized || name == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(params, "name", name);

    cJSON *arguments =
        arguments_json != NULL
            ? cJSON_Parse(arguments_json)
            : cJSON_CreateObject();

    if (arguments == NULL) {
        cJSON_Delete(params);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_AddItemToObject(params, "arguments", arguments);

    return request_method(
        "prompts/get",
        params,
        out_response_json
    );
}

esp_err_t local_mcp_client_call_tool(
    const char *tool_name,
    const char *arguments_json,
    char **out_response_json
)
{
    if (!s_initialized ||
        tool_name == NULL ||
        out_response_json == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    cJSON *params = cJSON_CreateObject();
    if (params == NULL) {
        return ESP_ERR_NO_MEM;
    }

    cJSON_AddStringToObject(params, "name", tool_name);

    cJSON *arguments =
        arguments_json != NULL
            ? cJSON_Parse(arguments_json)
            : cJSON_CreateObject();

    if (arguments == NULL ||
        !cJSON_IsObject(arguments)) {
        cJSON_Delete(arguments);
        cJSON_Delete(params);
        return ESP_ERR_INVALID_ARG;
    }

    cJSON_AddItemToObject(
        params,
        "arguments",
        arguments
    );

    return request_method(
        "tools/call",
        params,
        out_response_json
    );
}
