#include "storage.h"

#include <stdio.h>

#include "esp_log.h"
#include "esp_spiffs.h"

static const char *TAG = "storage";

esp_err_t storage_init(void)
{
    const esp_vfs_spiffs_conf_t config = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = false,
    };

    esp_err_t err = esp_vfs_spiffs_register(&config);
    if (err != ESP_OK) {
        if (err == ESP_FAIL) {
            ESP_LOGE(TAG, "No se pudo montar SPIFFS");
        } else if (err == ESP_ERR_NOT_FOUND) {
            ESP_LOGE(TAG, "No se encontro la particion SPIFFS 'storage'");
        } else {
            ESP_LOGE(TAG, "Error SPIFFS: %s", esp_err_to_name(err));
        }
        return err;
    }

    size_t total = 0;
    size_t used = 0;
    err = esp_spiffs_info("storage", &total, &used);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No se pudo leer el uso de SPIFFS: %s", esp_err_to_name(err));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "SPIFFS montado: total=%u bytes, usados=%u bytes",
             (unsigned) total, (unsigned) used);
    return ESP_OK;
}

bool storage_file_exists(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }

    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    fclose(file);
    return true;
}
