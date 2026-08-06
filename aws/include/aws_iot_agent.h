#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef esp_err_t (*aws_iot_task_handler_t)(
    const char *payload,
    size_t payload_len
);

esp_err_t aws_iot_agent_init(aws_iot_task_handler_t task_handler);
esp_err_t aws_iot_agent_start(void);

bool aws_iot_agent_is_connected(void);

esp_err_t aws_iot_agent_publish_result(const char *json_payload);
esp_err_t aws_iot_agent_publish_event(const char *json_payload);
esp_err_t aws_iot_agent_publish_status(const char *json_payload, bool retain);

const char *aws_iot_agent_get_tasks_topic(void);
const char *aws_iot_agent_get_results_topic(void);
const char *aws_iot_agent_get_events_topic(void);
const char *aws_iot_agent_get_status_topic(void);

#ifdef __cplusplus
}
#endif
