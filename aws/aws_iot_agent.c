#include "aws_iot_agent.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "esp_mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "aws_iot_agent";

extern const uint8_t aws_root_ca_start[]
    asm("_binary_certs_AmazonRootCA1_pem_start");
extern const uint8_t aws_root_ca_end[]
    asm("_binary_certs_AmazonRootCA1_pem_end");
extern const uint8_t aws_device_certificate_start[]
    asm("_binary_certs_device_certificate_pem_crt_start");
extern const uint8_t aws_device_certificate_end[]
    asm("_binary_certs_device_certificate_pem_crt_end");
extern const uint8_t aws_device_private_key_start[]
    asm("_binary_certs_device_private_pem_key_start");
extern const uint8_t aws_device_private_key_end[]
    asm("_binary_certs_device_private_pem_key_end");

typedef struct {
    size_t len;
    char payload[CONFIG_APP_AWS_MAX_TASK_BYTES + 1];
} aws_task_message_t;

static esp_mqtt_client_handle_t s_client;
static aws_iot_task_handler_t s_task_handler;
static QueueHandle_t s_task_queue;
static volatile bool s_connected;

static char s_tasks_topic[192];
static char s_results_topic[192];
static char s_events_topic[192];
static char s_status_topic[192];

static char s_rx_topic[192];
static char s_rx_buffer[CONFIG_APP_AWS_MAX_TASK_BYTES + 1];
static size_t s_rx_total;
static bool s_rx_active;

static const char s_offline_status[] =
    "{\"online\":false,\"reason\":\"mqtt_disconnect\"}";

static bool topic_equals(
    const char *event_topic,
    int event_topic_len,
    const char *expected
)
{
    if (event_topic == NULL || expected == NULL || event_topic_len < 0) {
        return false;
    }

    const size_t expected_len = strlen(expected);
    return expected_len == (size_t) event_topic_len &&
           memcmp(event_topic, expected, expected_len) == 0;
}

static esp_err_t publish_json(
    const char *topic,
    const char *payload,
    int qos,
    bool retain
)
{
    if (s_client == NULL || topic == NULL || payload == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_connected) {
        return ESP_ERR_INVALID_STATE;
    }

    const int message_id = esp_mqtt_client_publish(
        s_client,
        topic,
        payload,
        0,
        qos,
        retain ? 1 : 0
    );

    if (message_id < 0) {
        ESP_LOGE(TAG, "No se pudo publicar en %s", topic);
        return ESP_FAIL;
    }

    ESP_LOGD(TAG, "Publicado topic=%s message_id=%d", topic, message_id);
    return ESP_OK;
}

static void publish_online_status(void)
{
    char payload[512];

    snprintf(
        payload,
        sizeof(payload),
        "{"
        "\"online\":true,"
        "\"thing_name\":\"%s\","
        "\"agent_type\":\"embedded_ai_agent\","
        "\"local_mcp\":true,"
        "\"http_mcp\":%s,"
        "\"topic_prefix\":\"ai/agents\""
        "}",
        CONFIG_APP_THING_NAME,
#if CONFIG_APP_ENABLE_HTTP_MCP
        "true"
#else
        "false"
#endif
    );

    (void) aws_iot_agent_publish_status(payload, true);
}

static void task_worker(void *argument)
{
    (void) argument;

    aws_task_message_t message;

    while (true) {
        if (xQueueReceive(s_task_queue, &message, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (s_task_handler == NULL) {
            ESP_LOGW(TAG, "Tarea recibida sin handler registrado");
            continue;
        }

        const esp_err_t err = s_task_handler(
            message.payload,
            message.len
        );

        if (err != ESP_OK) {
            ESP_LOGE(TAG, "El agente rechazo la tarea: %s",
                     esp_err_to_name(err));
        }
    }
}

static void enqueue_complete_task(const char *payload, size_t payload_len)
{
    if (payload == NULL ||
        payload_len == 0 ||
        payload_len > CONFIG_APP_AWS_MAX_TASK_BYTES) {
        return;
    }

    aws_task_message_t message = {0};
    message.len = payload_len;
    memcpy(message.payload, payload, payload_len);
    message.payload[payload_len] = '\0';

    if (xQueueSend(s_task_queue, &message, 0) != pdTRUE) {
        ESP_LOGE(TAG, "Cola de tareas llena; mensaje descartado");
        (void) aws_iot_agent_publish_event(
            "{\"type\":\"task_rejected\",\"reason\":\"queue_full\"}"
        );
    }
}

static void handle_mqtt_data(esp_mqtt_event_handle_t event)
{
    if (event == NULL) {
        return;
    }

    if (event->current_data_offset == 0) {
        s_rx_active = false;
        s_rx_total = 0;
        s_rx_topic[0] = '\0';

        if (!topic_equals(
                event->topic,
                event->topic_len,
                s_tasks_topic
            )) {
            ESP_LOGD(TAG, "Mensaje ignorado en otro topic");
            return;
        }

        if (event->total_data_len <= 0 ||
            event->total_data_len > CONFIG_APP_AWS_MAX_TASK_BYTES) {
            ESP_LOGE(
                TAG,
                "Tarea demasiado grande: %d bytes, maximo=%d",
                event->total_data_len,
                CONFIG_APP_AWS_MAX_TASK_BYTES
            );
            return;
        }

        const size_t topic_len =
            event->topic_len < (int) (sizeof(s_rx_topic) - 1)
                ? (size_t) event->topic_len
                : sizeof(s_rx_topic) - 1;

        memcpy(s_rx_topic, event->topic, topic_len);
        s_rx_topic[topic_len] = '\0';

        s_rx_total = (size_t) event->total_data_len;
        s_rx_active = true;
    }

    if (!s_rx_active) {
        return;
    }

    if (event->current_data_offset < 0 ||
        event->data_len < 0 ||
        (size_t) event->current_data_offset +
            (size_t) event->data_len > s_rx_total) {
        ESP_LOGE(TAG, "Fragmento MQTT invalido");
        s_rx_active = false;
        return;
    }

    memcpy(
        s_rx_buffer + event->current_data_offset,
        event->data,
        (size_t) event->data_len
    );

    const size_t received_until =
        (size_t) event->current_data_offset +
        (size_t) event->data_len;

    if (received_until == s_rx_total) {
        s_rx_buffer[s_rx_total] = '\0';
        enqueue_complete_task(s_rx_buffer, s_rx_total);
        s_rx_active = false;
    }
}

static void mqtt_event_handler(
    void *handler_args,
    esp_event_base_t base,
    int32_t event_id,
    void *event_data
)
{
    (void) handler_args;
    (void) base;

    esp_mqtt_event_handle_t event =
        (esp_mqtt_event_handle_t) event_data;

    switch ((esp_mqtt_event_id_t) event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "Conectado a AWS IoT Core");

            if (esp_mqtt_client_subscribe(
                    s_client,
                    s_tasks_topic,
                    1
                ) < 0) {
                ESP_LOGE(TAG, "No se pudo suscribir a %s", s_tasks_topic);
            } else {
                ESP_LOGI(TAG, "Suscrito a %s", s_tasks_topic);
            }

            publish_online_status();
            break;

        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "Desconectado de AWS IoT Core");
            break;

        case MQTT_EVENT_DATA:
            handle_mqtt_data(event);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "Error MQTT/TLS");
            if (event != NULL &&
                event->error_handle != NULL &&
                event->error_handle->error_type ==
                    MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(
                    TAG,
                    "esp_tls_last_esp_err=0x%x, "
                    "tls_stack_err=0x%x, sock_errno=%d",
                    event->error_handle->esp_tls_last_esp_err,
                    event->error_handle->esp_tls_stack_err,
                    event->error_handle->esp_transport_sock_errno
                );
            }
            break;

        default:
            break;
    }
}

esp_err_t aws_iot_agent_init(aws_iot_task_handler_t task_handler)
{
    if (task_handler == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(CONFIG_APP_AWS_IOT_ENDPOINT) < 10 ||
        strstr(CONFIG_APP_AWS_IOT_ENDPOINT, "CAMBIAR") != NULL ||
        strlen(CONFIG_APP_THING_NAME) < 3 ||
        strstr(CONFIG_APP_THING_NAME, "CAMBIAR") != NULL) {
        ESP_LOGE(
            TAG,
            "Configura AWS endpoint y ThingName mediante idf.py menuconfig"
        );
        return ESP_ERR_INVALID_STATE;
    }

    s_task_handler = task_handler;

    snprintf(
        s_tasks_topic,
        sizeof(s_tasks_topic),
        "ai/agents/%s/tasks",
        CONFIG_APP_THING_NAME
    );
    snprintf(
        s_results_topic,
        sizeof(s_results_topic),
        "ai/agents/%s/results",
        CONFIG_APP_THING_NAME
    );
    snprintf(
        s_events_topic,
        sizeof(s_events_topic),
        "ai/agents/%s/events",
        CONFIG_APP_THING_NAME
    );
    snprintf(
        s_status_topic,
        sizeof(s_status_topic),
        "ai/agents/%s/status",
        CONFIG_APP_THING_NAME
    );

    s_task_queue = xQueueCreate(
        CONFIG_APP_AWS_TASK_QUEUE_LENGTH,
        sizeof(aws_task_message_t)
    );
    if (s_task_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(
            task_worker,
            "aws_task_worker",
            8192,
            NULL,
            6,
            NULL
        ) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    const esp_mqtt_client_config_t mqtt_config = {
        .broker = {
            .address = {
                .hostname = CONFIG_APP_AWS_IOT_ENDPOINT,
                .transport = MQTT_TRANSPORT_OVER_SSL,
                .port = 8883,
            },
            .verification = {
                .certificate = (const char *) aws_root_ca_start,
                .skip_cert_common_name_check = false,
            },
        },
        .credentials = {
            .client_id = CONFIG_APP_THING_NAME,
            .authentication = {
                .certificate =
                    (const char *) aws_device_certificate_start,
                .key =
                    (const char *) aws_device_private_key_start,
            },
        },
        .session = {
            .keepalive = 60,
            .disable_clean_session = false,
            .last_will = {
                .topic = s_status_topic,
                .msg = s_offline_status,
                .msg_len = 0,
                .qos = 1,
                .retain = 1,
            },
        },
        .network = {
            .reconnect_timeout_ms = 5000,
            .timeout_ms = 15000,
            .disable_auto_reconnect = false,
        },
        .task = {
            .priority = 5,
            .stack_size = 8192,
        },
        .buffer = {
            .size = CONFIG_APP_AWS_MQTT_BUFFER_BYTES,
            .out_size = CONFIG_APP_AWS_MQTT_BUFFER_BYTES,
        },
    };

    s_client = esp_mqtt_client_init(&mqtt_config);
    if (s_client == NULL) {
        return ESP_ERR_NO_MEM;
    }

    return esp_mqtt_client_register_event(
        s_client,
        ESP_EVENT_ANY_ID,
        mqtt_event_handler,
        NULL
    );
}

esp_err_t aws_iot_agent_start(void)
{
    if (s_client == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "ThingName/clientId: %s", CONFIG_APP_THING_NAME);
    ESP_LOGI(TAG, "Endpoint: %s:8883", CONFIG_APP_AWS_IOT_ENDPOINT);
    ESP_LOGI(TAG, "Tasks: %s", s_tasks_topic);

    return esp_mqtt_client_start(s_client);
}

bool aws_iot_agent_is_connected(void)
{
    return s_connected;
}

esp_err_t aws_iot_agent_publish_result(const char *json_payload)
{
    return publish_json(s_results_topic, json_payload, 1, false);
}

esp_err_t aws_iot_agent_publish_event(const char *json_payload)
{
    return publish_json(s_events_topic, json_payload, 1, false);
}

esp_err_t aws_iot_agent_publish_status(
    const char *json_payload,
    bool retain
)
{
    return publish_json(s_status_topic, json_payload, 1, retain);
}

const char *aws_iot_agent_get_tasks_topic(void)
{
    return s_tasks_topic;
}

const char *aws_iot_agent_get_results_topic(void)
{
    return s_results_topic;
}

const char *aws_iot_agent_get_events_topic(void)
{
    return s_events_topic;
}

const char *aws_iot_agent_get_status_topic(void)
{
    return s_status_topic;
}
