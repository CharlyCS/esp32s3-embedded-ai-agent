#include "time_sync.h"

#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "time_sync";

esp_err_t time_sync_wait(void)
{
    time_t now = 0;
    struct tm time_info = {0};

    time(&now);
    localtime_r(&now, &time_info);

    if (time_info.tm_year >= (2024 - 1900)) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Sincronizando hora por SNTP para validar TLS");

    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();

    for (int retry = 0; retry < 30; retry++) {
        time(&now);
        localtime_r(&now, &time_info);

        if (time_info.tm_year >= (2024 - 1900)) {
            ESP_LOGI(
                TAG,
                "Hora sincronizada: %04d-%02d-%02d %02d:%02d:%02d UTC",
                time_info.tm_year + 1900,
                time_info.tm_mon + 1,
                time_info.tm_mday,
                time_info.tm_hour,
                time_info.tm_min,
                time_info.tm_sec
            );
            return ESP_OK;
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGE(TAG, "No se pudo sincronizar la hora");
    return ESP_ERR_TIMEOUT;
}
