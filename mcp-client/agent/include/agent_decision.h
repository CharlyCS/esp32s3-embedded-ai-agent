#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AGENT_DECISION_INVALID = 0,
    AGENT_DECISION_TOOL_CALL,
    AGENT_DECISION_FINAL,
} agent_decision_type_t;

typedef struct {
    agent_decision_type_t type;
    char tool_name[96];
    char arguments_json[768];
    char reason[384];
    char answer[1024];
} agent_decision_t;

void agent_decision_reset(agent_decision_t *decision);

esp_err_t agent_decision_parse_json(
    const char *json_text,
    agent_decision_t *decision
);

#ifdef __cplusplus
}
#endif
