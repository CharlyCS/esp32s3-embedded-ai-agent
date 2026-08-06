#include "mcp_server_service.h"

#include <stdlib.h>
#include <string.h>

#include "mcp_tools.h"

#include "driver/gpio.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_mcp_engine.h"
#include "esp_mcp_mgr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "mcp_server";

static esp_mcp_t *s_local_engine;
static SemaphoreHandle_t s_local_engine_mutex;

#if CONFIG_APP_ENABLE_HTTP_MCP
static esp_mcp_t *s_http_engine;
static esp_mcp_mgr_handle_t s_http_manager;
static httpd_config_t s_httpd_config;
#endif

static esp_err_t create_engine(esp_mcp_t **out_engine)
{
    if (out_engine == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_mcp_t *engine = NULL;
    esp_err_t err = esp_mcp_create(&engine);
    if (err != ESP_OK) {
        return err;
    }

    err = mcp_tools_register_all(engine);
    if (err != ESP_OK) {
        (void) esp_mcp_destroy(engine);
        return err;
    }

    *out_engine = engine;
    return ESP_OK;
}

esp_err_t mcp_server_service_init(void)
{
    s_local_engine_mutex = xSemaphoreCreateMutex();
    if (s_local_engine_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    const esp_err_t err = create_engine(&s_local_engine);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo crear engine local: %s",
                 esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(
        TAG,
        "MCP Server local listo; transporte interno en proceso"
    );
    return ESP_OK;
}

esp_err_t mcp_server_process_local_json(
    const char *request_json,
    char **out_response_json
)
{
    if (request_json == NULL || out_response_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_response_json = NULL;

    const size_t request_len = strlen(request_json);
    if (request_len == 0 || request_len > UINT16_MAX) {
        return ESP_ERR_INVALID_SIZE;
    }

    if (s_local_engine == NULL || s_local_engine_mutex == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(
            s_local_engine_mutex,
            pdMS_TO_TICKS(30000)
        ) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    uint8_t *engine_response = NULL;
    uint16_t engine_response_len = 0;

    const esp_err_t err = esp_mcp_handle_message(
        s_local_engine,
        (const uint8_t *) request_json,
        (uint16_t) request_len,
        &engine_response,
        &engine_response_len
    );

    if (err == ESP_OK &&
        engine_response != NULL &&
        engine_response_len > 0) {
        char *copy = malloc((size_t) engine_response_len + 1);
        if (copy == NULL) {
            (void) esp_mcp_free_response(
                s_local_engine,
                engine_response
            );
            xSemaphoreGive(s_local_engine_mutex);
            return ESP_ERR_NO_MEM;
        }

        memcpy(copy, engine_response, engine_response_len);
        copy[engine_response_len] = '\0';
        *out_response_json = copy;
    }

    if (engine_response != NULL) {
        (void) esp_mcp_free_response(
            s_local_engine,
            engine_response
        );
    }

    xSemaphoreGive(s_local_engine_mutex);
    return err;
}

esp_err_t mcp_server_start_http(void)
{
#if !CONFIG_APP_ENABLE_HTTP_MCP
    ESP_LOGI(TAG, "HTTP MCP desactivado en menuconfig");
    return ESP_OK;
#else
    esp_err_t err = create_engine(&s_http_engine);
    if (err != ESP_OK) {
        return err;
    }

    s_httpd_config = HTTPD_DEFAULT_CONFIG();
    s_httpd_config.server_port = CONFIG_APP_HTTP_MCP_PORT;
    s_httpd_config.stack_size = 10240;
    s_httpd_config.max_uri_handlers = 16;
    s_httpd_config.lru_purge_enable = true;

    const esp_mcp_mgr_config_t manager_config = {
        .transport = esp_mcp_transport_http_server,
        .config = &s_httpd_config,
        .instance = s_http_engine,
    };

    err = esp_mcp_mgr_init(
        manager_config,
        &s_http_manager
    );
    if (err != ESP_OK) {
        return err;
    }

    err = esp_mcp_mgr_start(s_http_manager);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_mcp_mgr_register_endpoint(
        s_http_manager,
        CONFIG_APP_HTTP_MCP_ENDPOINT,
        NULL
    );
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(
        TAG,
        "MCP HTTP local iniciado en puerto %d, endpoint /%s",
        CONFIG_APP_HTTP_MCP_PORT,
        CONFIG_APP_HTTP_MCP_ENDPOINT
    );
    return ESP_OK;
#endif
}
