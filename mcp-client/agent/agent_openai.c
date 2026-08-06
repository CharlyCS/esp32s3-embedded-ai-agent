#include "agent_openai.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "OpenAI.h"
#include "esp_log.h"

static const char *TAG = "agent_openai";

static const char *SYSTEM_PROMPT =
    "Eres un agente de IA embebido en un ESP32-S3. "
    "Tu trabajo es analizar el objetivo recibido desde un supervisor y "
    "decidir si necesitas usar una herramienta MCP local. "
    "Reglas: "
    "1. Si puedes responder sin herramienta, responde type=final. "
    "2. Si necesitas una tool, responde type=tool_call. "
    "3. Usa exactamente un nombre incluido en tools disponibles. "
    "4. Devuelve solo JSON valido. "
    "5. No inventes tools ni argumentos. "
    "6. Si ya existe resultado MCP, genera type=final usando ese resultado. "
    "7. Tu respuesta debe estar en espanol.";

static char *build_prompt(
    const char *goal,
    const char *context_json,
    const char *available_tools_json,
    const char *mcp_result_json
)
{
    const char *context =
        context_json != NULL ? context_json : "{}";
    const char *tools =
        available_tools_json != NULL
            ? available_tools_json
            : "{\"tools\":[]}";
    const char *result =
        mcp_result_json != NULL
            ? mcp_result_json
            : "No hay resultado MCP previo.";

    const char *template_text =
        "Objetivo recibido:\n%s\n\n"
        "Contexto:\n%s\n\n"
        "Tools MCP disponibles, obtenidas del MCP Server local:\n%s\n\n"
        "Resultado MCP previo:\n%s\n\n"
        "Devuelve SOLO JSON valido.\n\n"
        "Para llamar una tool:\n"
        "{"
        "\"type\":\"tool_call\","
        "\"tool_name\":\"nombre_exacto\","
        "\"arguments\":{},"
        "\"reason\":\"explicacion breve\""
        "}\n\n"
        "Para terminar:\n"
        "{"
        "\"type\":\"final\","
        "\"answer\":\"respuesta final\""
        "}";

    const size_t required =
        strlen(template_text) +
        strlen(goal) +
        strlen(context) +
        strlen(tools) +
        strlen(result) +
        64;

    char *prompt = malloc(required);
    if (prompt == NULL) {
        return NULL;
    }

    snprintf(
        prompt,
        required,
        template_text,
        goal,
        context,
        tools,
        result
    );

    return prompt;
}

esp_err_t agent_openai_decide(
    const char *goal,
    const char *context_json,
    const char *available_tools_json,
    const char *mcp_result_json,
    agent_decision_t *decision
)
{
#if !CONFIG_APP_AGENT_USE_OPENAI
    (void) goal;
    (void) context_json;
    (void) available_tools_json;
    (void) mcp_result_json;
    (void) decision;
    return ESP_ERR_NOT_SUPPORTED;
#else
    if (goal == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(CONFIG_APP_AGENT_OPENAI_API_KEY) < 20 ||
        strstr(CONFIG_APP_AGENT_OPENAI_API_KEY, "CAMBIAR") != NULL) {
        ESP_LOGW(TAG, "OpenAI activado, pero la API key no esta configurada");
        return ESP_ERR_INVALID_STATE;
    }

    char *prompt = build_prompt(
        goal,
        context_json,
        available_tools_json,
        mcp_result_json
    );
    if (prompt == NULL) {
        return ESP_ERR_NO_MEM;
    }

    OpenAI_t *openai =
        OpenAICreate(CONFIG_APP_AGENT_OPENAI_API_KEY);
    if (openai == NULL) {
        free(prompt);
        return ESP_ERR_NO_MEM;
    }

    OpenAI_ChatCompletion_t *chat =
        openai->chatCreate(openai);
    if (chat == NULL) {
        OpenAIDelete(openai);
        free(prompt);
        return ESP_ERR_NO_MEM;
    }

    chat->setModel(chat, CONFIG_APP_AGENT_OPENAI_MODEL);
    chat->setSystem(chat, SYSTEM_PROMPT);
    chat->setMaxTokens(chat, 500);
    chat->setTemperature(chat, 0.1f);
    chat->setUser(chat, CONFIG_APP_THING_NAME);

    OpenAI_StringResponse_t *response =
        chat->message(chat, prompt, false);
    free(prompt);

    esp_err_t result = ESP_FAIL;

    if (response == NULL) {
        ESP_LOGE(TAG, "OpenAI no devolvio respuesta");
    } else if (response->getLen(response) > 0) {
        char *text = response->getData(response, 0);
        if (text != NULL) {
            ESP_LOGI(TAG, "Decision IA recibida");
            result = agent_decision_parse_json(
                text,
                decision
            );
        }
    } else if (response->getError(response) != NULL) {
        ESP_LOGE(
            TAG,
            "OpenAI error: %s",
            response->getError(response)
        );
    }

    if (response != NULL) {
        response->deleteResponse(response);
    }

    openai->chatDelete(chat);
    OpenAIDelete(openai);
    return result;
#endif
}
