#include "audio_service.h"
#include "aws_iot_agent.h"
#include "mcp_server_service.h"
#include "storage.h"
#include "time_sync.h"
#include "wifi_manager.h"

#include <stdlib.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

static esp_err_t process_mcp_from_aws(
    const char *request_json,
    size_t request_len
)
{
    (void) request_len;

    char *response_json = NULL;
    const esp_err_t err = mcp_server_process_local_json(
        request_json,
        &response_json
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MCP request via AWS failed: %s",
                 esp_err_to_name(err));
        return err;
    }

    if (response_json == NULL) {
        ESP_LOGD(TAG, "MCP notification processed without response");
        return ESP_OK;
    }

    const esp_err_t publish_err =
        aws_iot_agent_publish_result(response_json);
    free(response_json);
    return publish_err;
}

static esp_err_t initialize_nvs(void)
{
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    return err;
}

void app_main(void)
{
    ESP_LOGI(
        TAG,
        "ESP32-S3 MCP Server - Thing %s",
        CONFIG_APP_THING_NAME
    );

    ESP_ERROR_CHECK(initialize_nvs());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    /*
     * 1. Physical layer and MCP engine are initialized locally.
     */
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(audio_service_init());
    ESP_ERROR_CHECK(mcp_server_service_init());

#if CONFIG_APP_PLAY_STARTUP_AUDIO
    const esp_err_t startup_err =
        audio_service_play(CONFIG_APP_AUDIO_STARTUP_FILE);

    if (startup_err != ESP_OK) {
        ESP_LOGW(
            TAG,
            "inicio.wav no se reprodujo: %s",
            esp_err_to_name(startup_err)
        );
    }
#endif

    /*
     * 2. Network is used by AWS IoT MQTT/TLS and the optional LAN MCP
     *    Streamable HTTP endpoint.
     */
    ESP_ERROR_CHECK(wifi_manager_connect());

    const esp_err_t time_err = time_sync_wait();
    if (time_err != ESP_OK) {
        ESP_LOGW(
            TAG,
            "Continuando sin hora confirmada; AWS TLS puede fallar"
        );
    }

    ESP_ERROR_CHECK(mcp_server_start_http());

    /*
     * 3. AWS IoT transports raw MCP JSON-RPC messages over MQTT/TLS.
     *    The local adapter passes each message to the MCP server engine.
     */
    ESP_ERROR_CHECK(
        aws_iot_agent_init(process_mcp_from_aws)
    );
    ESP_ERROR_CHECK(aws_iot_agent_start());

    ESP_LOGI(TAG, "Arquitectura activa:");
    ESP_LOGI(TAG, "PC MCP Client -> AWS MQTT -> MCP Server -> Tool");
    ESP_LOGI(
        TAG,
        "Resultado -> Agent -> AWS topic %s",
        aws_iot_agent_get_results_topic()
    );
}
