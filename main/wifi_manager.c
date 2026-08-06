#include "wifi_manager.h"

#include <stdio.h>
#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_event_group;
static int s_retry_count;
static char s_ip_address[16] = "0.0.0.0";

static void wifi_event_handler(
    void *argument,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data
)
{
    (void) argument;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        (void) esp_wifi_connect();
        return;
    }

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < CONFIG_APP_WIFI_MAXIMUM_RETRY) {
            s_retry_count++;
            ESP_LOGW(
                TAG,
                "Wi-Fi desconectado; reintento %d/%d",
                s_retry_count,
                CONFIG_APP_WIFI_MAXIMUM_RETRY
            );
            (void) esp_wifi_connect();
        } else {
            xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        }
        return;
    }

    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        const ip_event_got_ip_t *event =
            (const ip_event_got_ip_t *) event_data;

        snprintf(
            s_ip_address,
            sizeof(s_ip_address),
            IPSTR,
            IP2STR(&event->ip_info.ip)
        );

        s_retry_count = 0;
        ESP_LOGI(TAG, "IP obtenida: %s", s_ip_address);
        xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_connect(void)
{
    s_event_group = xEventGroupCreate();
    if (s_event_group == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_create_default_wifi_sta();

    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&init_config);
    if (err != ESP_OK) {
        return err;
    }

    esp_event_handler_instance_t wifi_handler;
    esp_event_handler_instance_t ip_handler;

    err = esp_event_handler_instance_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        &wifi_event_handler,
        NULL,
        &wifi_handler
    );
    if (err != ESP_OK) {
        return err;
    }

    err = esp_event_handler_instance_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        &wifi_event_handler,
        NULL,
        &ip_handler
    );
    if (err != ESP_OK) {
        return err;
    }

    wifi_config_t wifi_config = {0};

    snprintf(
        (char *) wifi_config.sta.ssid,
        sizeof(wifi_config.sta.ssid),
        "%s",
        CONFIG_APP_WIFI_SSID
    );
    snprintf(
        (char *) wifi_config.sta.password,
        sizeof(wifi_config.sta.password),
        "%s",
        CONFIG_APP_WIFI_PASSWORD
    );

    wifi_config.sta.threshold.authmode =
        strlen(CONFIG_APP_WIFI_PASSWORD) > 0
            ? WIFI_AUTH_WPA2_PSK
            : WIFI_AUTH_OPEN;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) {
        return err;
    }

    err = esp_wifi_start();
    if (err != ESP_OK) {
        return err;
    }

    /*
     * Disable Wi-Fi power saving so MCP requests have lower latency and
     * ngrok/gateway connections remain responsive.
     */
    (void) esp_wifi_set_ps(WIFI_PS_NONE);

    ESP_LOGI(TAG, "Conectando a SSID: %s", CONFIG_APP_WIFI_SSID);

    const EventBits_t bits = xEventGroupWaitBits(
        s_event_group,
        WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE,
        pdFALSE,
        portMAX_DELAY
    );

    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "No se pudo conectar al Wi-Fi");
    return ESP_FAIL;
}

const char *wifi_manager_get_ip(void)
{
    return s_ip_address;
}
