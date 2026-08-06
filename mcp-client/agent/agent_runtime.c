#include "agent_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agent_decision.h"
#include "agent_openai.h"
#include "agent_rule_engine.h"
#include "aws_iot_agent.h"
#include "local_mcp_client.h"

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "agent_runtime";

typedef struct {
    size_t len;
    char payload[CONFIG_APP_AWS_MAX_TASK_BYTES + 1];
} agent_task_message_t;

static QueueHandle_t s_agent_queue;

#define TASK_HISTORY_SIZE 8
static char s_task_history[TASK_HISTORY_SIZE][96];
static size_t s_task_history_index;

static bool task_was_processed(const char *task_id)
{
    if (task_id == NULL || task_id[0] == '\0') {
        return false;
    }

    for (size_t index = 0;
         index < TASK_HISTORY_SIZE;
         index++) {
        if (strcmp(s_task_history[index], task_id) == 0) {
            return true;
        }
    }

    return false;
}

static void remember_task(const char *task_id)
{
    if (task_id == NULL || task_id[0] == '\0') {
        return;
    }

    snprintf(
        s_task_history[s_task_history_index],
        sizeof(s_task_history[s_task_history_index]),
        "%s",
        task_id
    );

    s_task_history_index =
        (s_task_history_index + 1) % TASK_HISTORY_SIZE;
}

static char *json_item_or_empty_object(
    const cJSON *item
)
{
    if (item == NULL) {
        return strdup("{}");
    }

    char *text = cJSON_PrintUnformatted(item);
    return text != NULL ? text : strdup("{}");
}

static esp_err_t decide_with_fallback(
    const char *goal,
    const char *context_json,
    const char *tools_json,
    const char *mcp_result_json,
    agent_decision_t *decision
)
{
    esp_err_t err = ESP_ERR_NOT_SUPPORTED;

#if CONFIG_APP_AGENT_USE_OPENAI
    err = agent_openai_decide(
        goal,
        context_json,
        tools_json,
        mcp_result_json,
        decision
    );

    if (err == ESP_OK) {
        return ESP_OK;
    }

    ESP_LOGW(
        TAG,
        "Decision IA fallo (%s); intentando fallback local",
        esp_err_to_name(err)
    );
#endif

#if CONFIG_APP_AGENT_ALLOW_RULE_FALLBACK
    return agent_rule_engine_decide(
        goal,
        context_json,
        tools_json,
        mcp_result_json,
        decision
    );
#else
    return err;
#endif
}

static void publish_task_result(
    const char *task_id,
    const char *goal,
    const char *status,
    const agent_decision_t *decision,
    const char *mcp_result_json,
    int64_t latency_ms,
    const char *error_message
)
{
    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return;
    }

    cJSON_AddStringToObject(
        root,
        "task_id",
        task_id != NULL ? task_id : ""
    );
    cJSON_AddStringToObject(
        root,
        "agent_id",
        CONFIG_APP_THING_NAME
    );
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(
        root,
        "goal",
        goal != NULL ? goal : ""
    );
    cJSON_AddNumberToObject(
        root,
        "latency_ms",
        (double) latency_ms
    );

    if (decision != NULL) {
        cJSON *decision_json = cJSON_CreateObject();

        cJSON_AddStringToObject(
            decision_json,
            "type",
            decision->type == AGENT_DECISION_TOOL_CALL
                ? "tool_call"
                : "final"
        );
        cJSON_AddStringToObject(
            decision_json,
            "tool_name",
            decision->tool_name
        );
        cJSON_AddStringToObject(
            decision_json,
            "reason",
            decision->reason
        );
        cJSON_AddStringToObject(
            decision_json,
            "answer",
            decision->answer
        );

        cJSON *arguments =
            cJSON_Parse(decision->arguments_json);
        if (arguments == NULL) {
            arguments = cJSON_CreateObject();
        }
        cJSON_AddItemToObject(
            decision_json,
            "arguments",
            arguments
        );
        cJSON_AddItemToObject(
            root,
            "decision",
            decision_json
        );
    }

    if (mcp_result_json != NULL) {
        cJSON *mcp_result =
            cJSON_Parse(mcp_result_json);

        if (mcp_result != NULL) {
            cJSON_AddItemToObject(
                root,
                "mcp_result",
                mcp_result
            );
        } else {
            cJSON_AddStringToObject(
                root,
                "mcp_result",
                mcp_result_json
            );
        }
    }

    if (error_message != NULL) {
        cJSON_AddStringToObject(
            root,
            "error",
            error_message
        );
    }

    char *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (payload != NULL) {
        const esp_err_t publish_err =
            aws_iot_agent_publish_result(payload);

        if (publish_err != ESP_OK) {
            ESP_LOGE(
                TAG,
                "No se pudo publicar resultado: %s",
                esp_err_to_name(publish_err)
            );
        }
        free(payload);
    }
}

static void process_task(const char *payload)
{
    const int64_t started_ms =
        esp_timer_get_time() / 1000;

    cJSON *task = cJSON_Parse(payload);
    if (task == NULL) {
        (void) aws_iot_agent_publish_event(
            "{\"type\":\"invalid_task\",\"reason\":\"invalid_json\"}"
        );
        return;
    }

    const cJSON *task_id_json =
        cJSON_GetObjectItemCaseSensitive(task, "task_id");
    const cJSON *goal_json =
        cJSON_GetObjectItemCaseSensitive(task, "goal");
    const cJSON *context_json =
        cJSON_GetObjectItemCaseSensitive(task, "context");

    if (!cJSON_IsString(task_id_json) ||
        task_id_json->valuestring == NULL ||
        !cJSON_IsString(goal_json) ||
        goal_json->valuestring == NULL) {
        cJSON_Delete(task);
        (void) aws_iot_agent_publish_event(
            "{"
            "\"type\":\"invalid_task\","
            "\"reason\":\"task_id_and_goal_are_required\""
            "}"
        );
        return;
    }

    const char *task_id = task_id_json->valuestring;
    const char *goal = goal_json->valuestring;

    if (task_was_processed(task_id)) {
        ESP_LOGW(TAG, "Tarea duplicada ignorada: %s", task_id);
        cJSON_Delete(task);
        return;
    }

    char *context_text =
        json_item_or_empty_object(context_json);
    char *tools_response = NULL;

    esp_err_t err =
        local_mcp_client_list_tools(&tools_response);

    if (err != ESP_OK) {
        publish_task_result(
            task_id,
            goal,
            "failed",
            NULL,
            NULL,
            (esp_timer_get_time() / 1000) - started_ms,
            "No se pudo consultar tools/list al MCP Server local"
        );
        free(context_text);
        free(tools_response);
        cJSON_Delete(task);
        return;
    }

    agent_decision_t decision;
    agent_decision_reset(&decision);

    err = decide_with_fallback(
        goal,
        context_text,
        tools_response,
        NULL,
        &decision
    );

    if (err != ESP_OK) {
        publish_task_result(
            task_id,
            goal,
            "failed",
            &decision,
            NULL,
            (esp_timer_get_time() / 1000) - started_ms,
            "El agente no pudo tomar una decision"
        );
        free(context_text);
        free(tools_response);
        cJSON_Delete(task);
        return;
    }

    char *mcp_result = NULL;

    if (decision.type == AGENT_DECISION_TOOL_CALL) {
        err = local_mcp_client_call_tool(
            decision.tool_name,
            decision.arguments_json,
            &mcp_result
        );

        if (err != ESP_OK) {
            publish_task_result(
                task_id,
                goal,
                "failed",
                &decision,
                mcp_result,
                (esp_timer_get_time() / 1000) - started_ms,
                "La tool MCP local devolvio un error"
            );
            free(mcp_result);
            free(context_text);
            free(tools_response);
            cJSON_Delete(task);
            return;
        }

#if CONFIG_APP_AGENT_OPENAI_FINAL_SUMMARY
        agent_decision_t final_decision;
        agent_decision_reset(&final_decision);

        if (decide_with_fallback(
                goal,
                context_text,
                tools_response,
                mcp_result,
                &final_decision
            ) == ESP_OK &&
            final_decision.type == AGENT_DECISION_FINAL) {
            decision = final_decision;
        }
#endif
    }

    remember_task(task_id);

    publish_task_result(
        task_id,
        goal,
        "completed",
        &decision,
        mcp_result,
        (esp_timer_get_time() / 1000) - started_ms,
        NULL
    );

    free(mcp_result);
    free(context_text);
    free(tools_response);
    cJSON_Delete(task);
}

static void agent_worker(void *argument)
{
    (void) argument;

    agent_task_message_t message;

    while (true) {
        if (xQueueReceive(
                s_agent_queue,
                &message,
                portMAX_DELAY
            ) != pdTRUE) {
            continue;
        }

        ESP_LOGI(TAG, "Procesando objetivo del supervisor");
        process_task(message.payload);
    }
}

esp_err_t agent_runtime_init(void)
{
    s_agent_queue = xQueueCreate(
        CONFIG_APP_AGENT_TASK_QUEUE_LENGTH,
        sizeof(agent_task_message_t)
    );

    if (s_agent_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(
            agent_worker,
            "embedded_agent",
            CONFIG_APP_AGENT_TASK_STACK,
            NULL,
            5,
            NULL
        ) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(
        TAG,
        "Agent Runtime listo: DECIDE -> MCP TOOL -> FINAL"
    );
    return ESP_OK;
}

esp_err_t agent_runtime_submit_task(
    const char *payload,
    size_t payload_len
)
{
    if (payload == NULL ||
        payload_len == 0 ||
        payload_len > CONFIG_APP_AWS_MAX_TASK_BYTES) {
        return ESP_ERR_INVALID_ARG;
    }

    agent_task_message_t message = {0};
    message.len = payload_len;
    memcpy(message.payload, payload, payload_len);
    message.payload[payload_len] = '\0';

    return xQueueSend(
               s_agent_queue,
               &message,
               pdMS_TO_TICKS(100)
           ) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}
