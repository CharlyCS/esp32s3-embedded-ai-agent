#pragma once

#include "agent_decision.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t agent_rule_engine_decide(
    const char *goal,
    const char *context_json,
    const char *available_tools_json,
    const char *mcp_result_json,
    agent_decision_t *decision
);

#ifdef __cplusplus
}
#endif
