#include "agent_decision.h"

#include <stdio.h>
#include <string.h>

#include "cJSON.h"

static void copy_json_field(
    const cJSON *object,
    const char *name,
    char *destination,
    size_t destination_size
)
{
    const cJSON *field =
        cJSON_GetObjectItemCaseSensitive(object, name);

    if (cJSON_IsString(field) &&
        field->valuestring != NULL) {
        snprintf(
            destination,
            destination_size,
            "%s",
            field->valuestring
        );
    }
}

void agent_decision_reset(agent_decision_t *decision)
{
    if (decision == NULL) {
        return;
    }

    memset(decision, 0, sizeof(*decision));
    decision->type = AGENT_DECISION_INVALID;
    snprintf(
        decision->arguments_json,
        sizeof(decision->arguments_json),
        "{}"
    );
}

esp_err_t agent_decision_parse_json(const char *json_text,agent_decision_t *decision)
{
    if (json_text == NULL || decision == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    agent_decision_reset(decision);

    const char *first_brace = strchr(json_text, '{');
    const char *last_brace = strrchr(json_text, '}');

    if (first_brace == NULL ||
        last_brace == NULL ||
        last_brace < first_brace) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const size_t json_len =
        (size_t) (last_brace - first_brace + 1);

    char *clean_json = malloc(json_len + 1);
    if (clean_json == NULL) {
        return ESP_ERR_NO_MEM;
    }

    memcpy(clean_json, first_brace, json_len);
    clean_json[json_len] = '\0';

    cJSON *root = cJSON_Parse(clean_json);
    free(clean_json);

    if (root == NULL) {
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *type =
        cJSON_GetObjectItemCaseSensitive(root, "type");

    if (!cJSON_IsString(type) ||
        type->valuestring == NULL) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (strcmp(type->valuestring, "tool_call") == 0) {
        decision->type = AGENT_DECISION_TOOL_CALL;
        copy_json_field(
            root,
            "tool_name",
            decision->tool_name,
            sizeof(decision->tool_name)
        );
        copy_json_field(
            root,
            "reason",
            decision->reason,
            sizeof(decision->reason)
        );

        const cJSON *arguments =
            cJSON_GetObjectItemCaseSensitive(
                root,
                "arguments"
            );

        if (arguments != NULL) {
            char *arguments_text =
                cJSON_PrintUnformatted(arguments);

            if (arguments_text != NULL) {
                snprintf(
                    decision->arguments_json,
                    sizeof(decision->arguments_json),
                    "%s",
                    arguments_text
                );
                free(arguments_text);
            }
        }

        if (decision->tool_name[0] == '\0') {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else if (strcmp(type->valuestring, "final") == 0) {
        decision->type = AGENT_DECISION_FINAL;
        copy_json_field(
            root,
            "answer",
            decision->answer,
            sizeof(decision->answer)
        );

        if (decision->answer[0] == '\0') {
            cJSON_Delete(root);
            return ESP_ERR_INVALID_RESPONSE;
        }
    } else {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON_Delete(root);
    return ESP_OK;
}
