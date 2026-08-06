#include "mcp_tools.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "audio_service.h"
#include "storage.h"

#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mcp_data.h"
#include "esp_mcp_prompt.h"
#include "esp_mcp_property.h"
#include "esp_mcp_resource.h"
#include "esp_mcp_tool.h"
#include "esp_timer.h"

static const char *TAG = "mcp_tools";

static cJSON *build_status_json(void)
{
    audio_status_t audio_status;
    audio_service_get_status(&audio_status);

    cJSON *root = cJSON_CreateObject();
    cJSON *audio = cJSON_CreateObject();
    cJSON *files = cJSON_CreateObject();
    cJSON *memory = cJSON_CreateObject();

    if (root == NULL || audio == NULL ||
        files == NULL || memory == NULL) {
        cJSON_Delete(root);
        cJSON_Delete(audio);
        cJSON_Delete(files);
        cJSON_Delete(memory);
        return NULL;
    }

    cJSON_AddStringToObject(
        root,
        "device",
        "Waveshare ESP32-S3-AUDIO-Board"
    );
    cJSON_AddStringToObject(
        root,
        "thing_name",
        CONFIG_APP_THING_NAME
    );
    cJSON_AddStringToObject(
        root,
        "agent_type",
        "embedded_ai_agent"
    );
    cJSON_AddNumberToObject(
        root,
        "uptime_ms",
        (double) (esp_timer_get_time() / 1000)
    );

    cJSON_AddBoolToObject(
        audio,
        "initialized",
        audio_status.initialized
    );
    cJSON_AddBoolToObject(
        audio,
        "playing",
        audio_status.playing
    );
    cJSON_AddNumberToObject(
        audio,
        "volume",
        audio_status.volume
    );
    cJSON_AddStringToObject(
        audio,
        "current_file",
        audio_status.current_file
    );
    cJSON_AddStringToObject(
        audio,
        "last_error",
        audio_status.last_error
    );
    cJSON_AddItemToObject(root, "audio", audio);

    cJSON_AddBoolToObject(
        files,
        "startup_available",
        storage_file_exists(CONFIG_APP_AUDIO_STARTUP_FILE)
    );
    cJSON_AddBoolToObject(
        files,
        "welcome_available",
        storage_file_exists(CONFIG_APP_AUDIO_WELCOME_FILE)
    );
    cJSON_AddStringToObject(
        files,
        "startup_path",
        CONFIG_APP_AUDIO_STARTUP_FILE
    );
    cJSON_AddStringToObject(
        files,
        "welcome_path",
        CONFIG_APP_AUDIO_WELCOME_FILE
    );
    cJSON_AddItemToObject(root, "files", files);

    cJSON_AddNumberToObject(
        memory,
        "free_heap",
        (double) esp_get_free_heap_size()
    );
    cJSON_AddNumberToObject(
        memory,
        "minimum_free_heap",
        (double) esp_get_minimum_free_heap_size()
    );
    cJSON_AddItemToObject(root, "memory", memory);

    return root;
}

static esp_mcp_value_t get_status_callback(
    const esp_mcp_property_list_t *properties
)
{
    (void) properties;

    cJSON *status = build_status_json();
    if (status == NULL) {
        return esp_mcp_value_create_string(
            "{\"success\":false,\"error\":\"out_of_memory\"}"
        );
    }

    char *text = cJSON_PrintUnformatted(status);
    cJSON_Delete(status);

    if (text == NULL) {
        return esp_mcp_value_create_string(
            "{\"success\":false,\"error\":\"serialization_failed\"}"
        );
    }

    const esp_mcp_value_t value =
        esp_mcp_value_create_string(text);
    free(text);
    return value;
}

static esp_mcp_value_t play_startup_callback(
    const esp_mcp_property_list_t *properties
)
{
    (void) properties;

    const esp_err_t err =
        audio_service_play(CONFIG_APP_AUDIO_STARTUP_FILE);

    ESP_LOGI(TAG, "play_startup: %s", esp_err_to_name(err));
    return esp_mcp_value_create_bool(err == ESP_OK);
}

static esp_mcp_value_t play_welcome_callback(
    const esp_mcp_property_list_t *properties
)
{
    (void) properties;

    const esp_err_t err =
        audio_service_play(CONFIG_APP_AUDIO_WELCOME_FILE);

    ESP_LOGI(TAG, "play_welcome: %s", esp_err_to_name(err));
    return esp_mcp_value_create_bool(err == ESP_OK);
}

static esp_mcp_value_t stop_audio_callback(
    const esp_mcp_property_list_t *properties
)
{
    (void) properties;

    return esp_mcp_value_create_bool(
        audio_service_stop() == ESP_OK
    );
}

static esp_mcp_value_t set_volume_callback(
    const esp_mcp_property_list_t *properties
)
{
    const int volume =
        esp_mcp_property_list_get_property_int(
            properties,
            "volume"
        );

    return esp_mcp_value_create_bool(
        audio_service_set_volume(volume) == ESP_OK
    );
}

static esp_err_t read_device_status_resource(
    const char *uri,
    char **out_mime,
    char **out_text,
    char **out_blob,
    void *context
)
{
    (void) uri;
    (void) context;

    if (out_mime == NULL || out_text == NULL || out_blob == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *status = build_status_json();
    if (status == NULL) {
        return ESP_ERR_NO_MEM;
    }

    *out_mime = strdup("application/json");
    *out_text = cJSON_PrintUnformatted(status);
    *out_blob = NULL;
    cJSON_Delete(status);

    if (*out_mime == NULL || *out_text == NULL) {
        free(*out_mime);
        free(*out_text);
        *out_mime = NULL;
        *out_text = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t render_welcome_prompt(
    const char *args_json,
    char **out_description,
    char **out_messages_json,
    void *context
)
{
    (void) args_json;
    (void) context;

    if (out_description == NULL || out_messages_json == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *out_description = strdup(
        "Plan local para recibir a una persona con el audio disponible"
    );
    *out_messages_json = strdup(
        "["
        "{"
        "\"role\":\"user\","
        "\"content\":{"
        "\"type\":\"text\","
        "\"text\":\"Verifica el estado del audio y reproduce la bienvenida\""
        "}"
        "}"
        "]"
    );

    if (*out_description == NULL || *out_messages_json == NULL) {
        free(*out_description);
        free(*out_messages_json);
        *out_description = NULL;
        *out_messages_json = NULL;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

static esp_err_t add_simple_tool(
    esp_mcp_t *mcp,
    const char *name,
    const char *description,
    esp_mcp_tool_callback_t callback
)
{
    esp_mcp_tool_t *tool =
        esp_mcp_tool_create(name, description, callback);

    if (tool == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return esp_mcp_add_tool(mcp, tool);
}

esp_err_t mcp_tools_register_all(esp_mcp_t *mcp)
{
    if (mcp == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = add_simple_tool(
        mcp,
        "self.get_device_status",
        "Devuelve el estado del agente, memoria, audio y archivos",
        get_status_callback
    );
    if (err != ESP_OK) {
        return err;
    }

    err = add_simple_tool(
        mcp,
        "self.audio.play_startup",
        "Reproduce el audio inicial inicio.wav",
        play_startup_callback
    );
    if (err != ESP_OK) {
        return err;
    }

    err = add_simple_tool(
        mcp,
        "self.audio.play_welcome",
        "Reproduce el audio de bienvenida bienvenida.wav",
        play_welcome_callback
    );
    if (err != ESP_OK) {
        return err;
    }

    err = add_simple_tool(
        mcp,
        "self.audio.stop",
        "Detiene la reproduccion de audio",
        stop_audio_callback
    );
    if (err != ESP_OK) {
        return err;
    }

    esp_mcp_tool_t *volume_tool = esp_mcp_tool_create(
        "self.audio.set_volume",
        "Configura el volumen del parlante entre 0 y 100",
        set_volume_callback
    );
    if (volume_tool == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_mcp_property_t *volume_property =
        esp_mcp_property_create_with_range("volume", 0, 100);
    if (volume_property == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_mcp_tool_add_property(
        volume_tool,
        volume_property
    );
    if (err != ESP_OK) {
        return err;
    }

    err = esp_mcp_add_tool(mcp, volume_tool);
    if (err != ESP_OK) {
        return err;
    }

    esp_mcp_resource_t *status_resource =
        esp_mcp_resource_create(
            "device://status",
            "device.status",
            "Estado del agente",
            "Estado actual del dispositivo, audio y memoria",
            "application/json",
            read_device_status_resource,
            NULL
        );
    if (status_resource == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_mcp_add_resource(mcp, status_resource);
    if (err != ESP_OK) {
        return err;
    }

    esp_mcp_prompt_t *welcome_prompt =
        esp_mcp_prompt_create(
            "welcome.plan",
            "Plan de bienvenida",
            "Prompt local para ejecutar una bienvenida",
            NULL,
            render_welcome_prompt,
            NULL
        );
    if (welcome_prompt == NULL) {
        return ESP_ERR_NO_MEM;
    }

    err = esp_mcp_add_prompt(mcp, welcome_prompt);
    if (err != ESP_OK) {
        return err;
    }

    ESP_LOGI(TAG, "Tools, resource y prompt MCP registrados");
    return ESP_OK;
}
