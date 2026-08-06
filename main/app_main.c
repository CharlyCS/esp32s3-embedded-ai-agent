#include "agent_runtime.h"
#include "audio_service.h"
#include "aws_iot_agent.h"
#include "local_mcp_client.h"
#include "mcp_server_service.h"
#include "storage.h"
#include "time_sync.h"
#include "wifi_manager.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char *TAG = "app_main";

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
        "ESP32-S3 Embedded AI Agent - Thing %s",
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

    /*
     * 2. MCP client initializes a real local MCP session with the server
     *    using an in-process JSON-RPC transport.
     */
    ESP_ERROR_CHECK(local_mcp_client_init());

    /*
     * 3. Agent Runtime is ready before AWS starts delivering tasks.
     */
    ESP_ERROR_CHECK(agent_runtime_init());

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
     * 4. Network is only used for communication with the supervisor,
     *    optional OpenAI reasoning, and optional LAN MCP diagnostics.
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
     * 5. AWS IoT transports high-level tasks/results. It does not replace
     *    the local MCP protocol between the Agent Runtime and its tools.
     */
    ESP_ERROR_CHECK(
        aws_iot_agent_init(agent_runtime_submit_task)
    );
    ESP_ERROR_CHECK(aws_iot_agent_start());

    ESP_LOGI(TAG, "Arquitectura activa:");
    ESP_LOGI(TAG, "AWS task -> Agent -> MCP Client -> MCP Server -> Tool");
    ESP_LOGI(
        TAG,
        "Resultado -> Agent -> AWS topic %s",
        aws_iot_agent_get_results_topic()
    );
}
