#include "agent_rule_engine.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void lowercase_ascii(
    const char *source,
    char *destination,
    size_t destination_size
)
{
    if (destination_size == 0) {
        return;
    }

    size_t index = 0;

    while (source != NULL &&
           source[index] != '\0' &&
           index + 1 < destination_size) {
        destination[index] =
            (char) tolower((unsigned char) source[index]);
        index++;
    }

    destination[index] = '\0';
}

static int find_first_number(const char *text)
{
    if (text == NULL) {
        return -1;
    }

    while (*text != '\0') {
        if (isdigit((unsigned char) *text)) {
            return atoi(text);
        }
        text++;
    }

    return -1;
}

esp_err_t agent_rule_engine_decide(
    const char *goal,
    const char *context_json,
    const char *available_tools_json,
    const char *mcp_result_json,
    agent_decision_t *decision
)
{
    (void) context_json;
    (void) available_tools_json;

    if (goal == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    agent_decision_reset(decision);

    if (mcp_result_json != NULL) {
        decision->type = AGENT_DECISION_FINAL;
        snprintf(
            decision->answer,
            sizeof(decision->answer),
            "La herramienta local termino. Resultado MCP: %.850s",
            mcp_result_json
        );
        return ESP_OK;
    }

    char normalized[1024];
    lowercase_ascii(goal, normalized, sizeof(normalized));

    decision->type = AGENT_DECISION_TOOL_CALL;

    if (strstr(normalized, "bienven") != NULL ||
        strstr(normalized, "welcome") != NULL ||
        strstr(normalized, "recib") != NULL) {
        snprintf(
            decision->tool_name,
            sizeof(decision->tool_name),
            "self.audio.play_welcome"
        );
        snprintf(
            decision->reason,
            sizeof(decision->reason),
            "El objetivo requiere ejecutar el audio local de bienvenida"
        );
        return ESP_OK;
    }

    if (strstr(normalized, "inicio") != NULL ||
        strstr(normalized, "arranque") != NULL ||
        strstr(normalized, "startup") != NULL) {
        snprintf(
            decision->tool_name,
            sizeof(decision->tool_name),
            "self.audio.play_startup"
        );
        snprintf(
            decision->reason,
            sizeof(decision->reason),
            "El objetivo solicita el audio inicial"
        );
        return ESP_OK;
    }

    if (strstr(normalized, "deten") != NULL ||
        strstr(normalized, "parar") != NULL ||
        strstr(normalized, "stop") != NULL) {
        snprintf(
            decision->tool_name,
            sizeof(decision->tool_name),
            "self.audio.stop"
        );
        snprintf(
            decision->reason,
            sizeof(decision->reason),
            "El objetivo solicita detener el audio"
        );
        return ESP_OK;
    }

    if (strstr(normalized, "volumen") != NULL ||
        strstr(normalized, "volume") != NULL) {
        int volume = find_first_number(normalized);
        if (volume < 0) {
            volume = CONFIG_APP_AUDIO_DEFAULT_VOLUME;
        }
        if (volume > 100) {
            volume = 100;
        }

        snprintf(
            decision->tool_name,
            sizeof(decision->tool_name),
            "self.audio.set_volume"
        );
        snprintf(
            decision->arguments_json,
            sizeof(decision->arguments_json),
            "{\"volume\":%d}",
            volume
        );
        snprintf(
            decision->reason,
            sizeof(decision->reason),
            "El objetivo solicita modificar el volumen"
        );
        return ESP_OK;
    }

    if (strstr(normalized, "estado") != NULL ||
        strstr(normalized, "status") != NULL ||
        strstr(normalized, "diagnost") != NULL) {
        snprintf(
            decision->tool_name,
            sizeof(decision->tool_name),
            "self.get_device_status"
        );
        snprintf(
            decision->reason,
            sizeof(decision->reason),
            "El objetivo requiere consultar el estado local"
        );
        return ESP_OK;
    }

    decision->type = AGENT_DECISION_FINAL;
    snprintf(
        decision->answer,
        sizeof(decision->answer),
        "No encontre una herramienta local adecuada para el objetivo recibido."
    );
    return ESP_OK;
}
