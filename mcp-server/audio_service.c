#include "audio_service.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "board_config.h"
#include "storage.h"
#include "tca9555.h"
#include "wav_reader.h"

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

typedef enum {
    AUDIO_COMMAND_PLAY = 0,
    AUDIO_COMMAND_STOP,
    AUDIO_COMMAND_SET_VOLUME,
} audio_command_type_t;

typedef struct {
    audio_command_type_t type;
    char path[128];
    int volume;
} audio_command_t;

static const char *TAG = "audio_service";

static QueueHandle_t s_command_queue;
static SemaphoreHandle_t s_status_mutex;
static audio_status_t s_status;

static i2c_master_bus_handle_t s_i2c_bus;
static i2s_chan_handle_t s_i2s_tx;
static tca9555_t s_expander;
static esp_codec_dev_handle_t s_codec;

static void set_status(
    bool playing,
    const char *current_file,
    const char *last_error
)
{
    if (s_status_mutex == NULL) {
        return;
    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.playing = playing;

    snprintf(
        s_status.current_file,
        sizeof(s_status.current_file),
        "%s",
        current_file != NULL ? current_file : ""
    );

    snprintf(
        s_status.last_error,
        sizeof(s_status.last_error),
        "%s",
        last_error != NULL ? last_error : ""
    );

    xSemaphoreGive(s_status_mutex);
}

static void set_pa_enabled(bool enabled)
{
    const bool level =
        BOARD_PA_ACTIVE_LEVEL ? enabled : !enabled;

    esp_err_t err = tca9555_set_output(
        &s_expander,
        BOARD_TCA9555_PA_EXIO,
        level
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo cambiar PA_EN: %s", esp_err_to_name(err));
    }
}

static esp_err_t init_i2c(void)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };

    return i2c_new_master_bus(&bus_config, &s_i2c_bus);
}

static esp_err_t init_i2s(void)
{
    const i2s_chan_config_t channel_config =
        I2S_CHANNEL_DEFAULT_CONFIG(BOARD_I2S_PORT, I2S_ROLE_MASTER);

    esp_err_t err = i2s_new_channel(
        &channel_config,
        &s_i2s_tx,
        NULL
    );
    if (err != ESP_OK) {
        return err;
    }

    i2s_std_config_t standard_config = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BOARD_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
            I2S_DATA_BIT_WIDTH_16BIT,
            I2S_SLOT_MODE_STEREO
        ),
        .gpio_cfg = {
            .mclk = BOARD_I2S_MCLK,
            .bclk = BOARD_I2S_BCLK,
            .ws = BOARD_I2S_WS,
            .dout = BOARD_I2S_DATA_OUT,
            .din = I2S_GPIO_UNUSED,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    standard_config.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    return i2s_channel_init_std_mode(s_i2s_tx, &standard_config);
}

static esp_err_t init_codec(void)
{
    audio_codec_i2s_cfg_t i2s_config = {
        .port = BOARD_I2S_PORT,
        .rx_handle = NULL,
        .tx_handle = s_i2s_tx,
        .clk_src = I2S_CLK_SRC_DEFAULT,
    };

    const audio_codec_data_if_t *data_interface =
        audio_codec_new_i2s_data(&i2s_config);
    if (data_interface == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la interfaz I2S del codec");
        return ESP_ERR_NO_MEM;
    }

    audio_codec_i2c_cfg_t i2c_config = {
        .port = BOARD_I2C_PORT,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = s_i2c_bus,
    };

    const audio_codec_ctrl_if_t *control_interface =
        audio_codec_new_i2c_ctrl(&i2c_config);
    if (control_interface == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la interfaz I2C del ES8311");
        return ESP_ERR_NO_MEM;
    }

    const audio_codec_gpio_if_t *gpio_interface =
        audio_codec_new_gpio();
    if (gpio_interface == NULL) {
        ESP_LOGE(TAG, "No se pudo crear la interfaz GPIO del codec");
        return ESP_ERR_NO_MEM;
    }

    es8311_codec_cfg_t codec_config = {
        .ctrl_if = control_interface,
        .gpio_if = gpio_interface,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,  // PA is controlled through TCA9555 EXIO8.
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .no_dac_ref = false,
        .mclk_div = 256,
    };

    const audio_codec_if_t *codec_interface =
        es8311_codec_new(&codec_config);
    if (codec_interface == NULL) {
        ESP_LOGE(TAG, "No se pudo crear el driver ES8311");
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_cfg_t device_config = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_interface,
        .data_if = data_interface,
    };

    s_codec = esp_codec_dev_new(&device_config);
    if (s_codec == NULL) {
        ESP_LOGE(TAG, "No se pudo crear esp_codec_dev");
        return ESP_ERR_NO_MEM;
    }

    esp_codec_dev_sample_info_t sample_info = {
        .bits_per_sample = BOARD_AUDIO_BITS,
        .channel = BOARD_AUDIO_CHANNELS,
        .channel_mask = 0,
        .sample_rate = BOARD_AUDIO_SAMPLE_RATE,
        .mclk_multiple = 256,
    };

    int codec_result = esp_codec_dev_open(s_codec, &sample_info);
    if (codec_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "No se pudo abrir ES8311: %d", codec_result);
        return ESP_FAIL;
    }

    codec_result = esp_codec_dev_set_out_vol(
        s_codec,
        CONFIG_APP_AUDIO_DEFAULT_VOLUME
    );
    if (codec_result != ESP_CODEC_DEV_OK) {
        ESP_LOGE(TAG, "No se pudo fijar volumen inicial: %d", codec_result);
        return ESP_FAIL;
    }

    (void) esp_codec_dev_set_out_mute(s_codec, true);
    return ESP_OK;
}

static void apply_volume(int volume)
{
    if (volume < 0 || volume > 100 || s_codec == NULL) {
        return;
    }

    const int result = esp_codec_dev_set_out_vol(s_codec, volume);
    if (result != ESP_CODEC_DEV_OK) {
        char error_text[64];
        snprintf(error_text, sizeof(error_text), "set_volume fallo: %d", result);
        set_status(false, "", error_text);
        ESP_LOGE(TAG, "%s", error_text);
        return;
    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.volume = volume;
    xSemaphoreGive(s_status_mutex);

    ESP_LOGI(TAG, "Volumen=%d", volume);
}

static bool process_pending_commands(
    audio_command_t *next_play,
    bool *stop_requested
)
{
    audio_command_t command;
    bool received_any = false;

    while (xQueueReceive(s_command_queue, &command, 0) == pdTRUE) {
        received_any = true;

        if (command.type == AUDIO_COMMAND_SET_VOLUME) {
            apply_volume(command.volume);
        } else if (command.type == AUDIO_COMMAND_STOP) {
            *stop_requested = true;
            next_play->path[0] = '\0';
        } else if (command.type == AUDIO_COMMAND_PLAY) {
            *stop_requested = true;
            *next_play = command;  // Latest play command wins.
        }
    }

    return received_any;
}

static esp_err_t play_wav_file(
    const char *path,
    audio_command_t *next_play
)
{
    wav_reader_t reader;
    esp_err_t err = wav_reader_open(&reader, path);
    if (err != ESP_OK) {
        char message[128];
        snprintf(
            message,
            sizeof(message),
            "WAV invalido o ausente: %s (%s)",
            path,
            esp_err_to_name(err)
        );
        set_status(false, "", message);
        ESP_LOGE(TAG, "%s", message);
        return err;
    }

    uint8_t *buffer = malloc(BOARD_AUDIO_CHUNK_BYTES);
    if (buffer == NULL) {
        wav_reader_close(&reader);
        set_status(false, "", "Sin memoria para buffer de audio");
        return ESP_ERR_NO_MEM;
    }

    set_status(true, path, "");
    ESP_LOGI(TAG, "Reproduciendo %s", path);

    /*
     * Anti-pop sequence:
     * 1. keep codec muted
     * 2. enable external amplifier through EXIO8
     * 3. wait briefly
     * 4. unmute codec
     */
    (void) esp_codec_dev_set_out_mute(s_codec, true);
    set_pa_enabled(true);
    vTaskDelay(pdMS_TO_TICKS(25));
    (void) esp_codec_dev_set_out_mute(s_codec, false);

    bool stop_requested = false;
    next_play->path[0] = '\0';

    while (!stop_requested) {
        const size_t bytes = wav_reader_read(
            &reader,
            buffer,
            BOARD_AUDIO_CHUNK_BYTES
        );

        if (bytes == 0) {
            break;
        }

        const int result = esp_codec_dev_write(s_codec, buffer, (int) bytes);
        if (result != ESP_CODEC_DEV_OK) {
            char message[96];
            snprintf(message, sizeof(message), "Error I2S/codec: %d", result);
            set_status(false, "", message);
            ESP_LOGE(TAG, "%s", message);
            err = ESP_FAIL;
            break;
        }

        (void) process_pending_commands(next_play, &stop_requested);
    }

    memset(buffer, 0, BOARD_AUDIO_CHUNK_BYTES);
    (void) esp_codec_dev_write(s_codec, buffer, 512);
    vTaskDelay(pdMS_TO_TICKS(20));

    (void) esp_codec_dev_set_out_mute(s_codec, true);
    vTaskDelay(pdMS_TO_TICKS(10));
    set_pa_enabled(false);

    free(buffer);
    wav_reader_close(&reader);

    if (err == ESP_OK) {
        set_status(false, "", "");
    }

    return err;
}

static void audio_task(void *argument)
{
    (void) argument;

    audio_command_t command;
    audio_command_t next_play;

    while (true) {
        if (xQueueReceive(s_command_queue, &command, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (command.type == AUDIO_COMMAND_SET_VOLUME) {
            apply_volume(command.volume);
            continue;
        }

        if (command.type == AUDIO_COMMAND_STOP) {
            set_status(false, "", "");
            continue;
        }

        if (command.type != AUDIO_COMMAND_PLAY) {
            continue;
        }

        while (command.type == AUDIO_COMMAND_PLAY) {
            memset(&next_play, 0, sizeof(next_play));
            (void) play_wav_file(command.path, &next_play);

            if (next_play.path[0] == '\0') {
                break;
            }

            command = next_play;
        }
    }
}

esp_err_t audio_service_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_status.volume = CONFIG_APP_AUDIO_DEFAULT_VOLUME;

    s_status_mutex = xSemaphoreCreateMutex();
    s_command_queue = xQueueCreate(
        BOARD_AUDIO_QUEUE_LENGTH,
        sizeof(audio_command_t)
    );

    if (s_status_mutex == NULL || s_command_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = init_i2c();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C fallo: %s", esp_err_to_name(err));
        return err;
    }

    err = tca9555_init(
        &s_expander,
        s_i2c_bus,
        BOARD_TCA9555_I2C_ADDRESS,
        BOARD_I2C_FREQUENCY_HZ
    );
    if (err != ESP_OK) {
        return err;
    }

    set_pa_enabled(false);

    err = init_i2s();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S fallo: %s", esp_err_to_name(err));
        return err;
    }

    err = init_codec();
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(
            audio_task,
            "audio_player",
            BOARD_AUDIO_TASK_STACK,
            NULL,
            BOARD_AUDIO_TASK_PRIORITY,
            NULL
        ) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    s_status.initialized = true;
    xSemaphoreGive(s_status_mutex);

    ESP_LOGI(
        TAG,
        "Audio listo: ES8311, %d Hz, mono, PCM16, DOUT=GPIO%d",
        BOARD_AUDIO_SAMPLE_RATE,
        BOARD_I2S_DATA_OUT
    );

    return ESP_OK;
}

esp_err_t audio_service_play(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    if (!storage_file_exists(path)) {
        ESP_LOGE(TAG, "Archivo no encontrado: %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    audio_command_t command = {
        .type = AUDIO_COMMAND_PLAY,
        .volume = 0,
    };
    snprintf(command.path, sizeof(command.path), "%s", path);

    return xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(200)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t audio_service_stop(void)
{
    audio_command_t command = {
        .type = AUDIO_COMMAND_STOP,
    };

    return xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(200)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

esp_err_t audio_service_set_volume(int volume)
{
    if (volume < 0 || volume > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    audio_command_t command = {
        .type = AUDIO_COMMAND_SET_VOLUME,
        .volume = volume,
    };

    return xQueueSend(s_command_queue, &command, pdMS_TO_TICKS(200)) == pdTRUE
               ? ESP_OK
               : ESP_ERR_TIMEOUT;
}

void audio_service_get_status(audio_status_t *status)
{
    if (status == NULL) {
        return;
    }

    if (s_status_mutex == NULL) {
        memset(status, 0, sizeof(*status));
        return;
    }

    xSemaphoreTake(s_status_mutex, portMAX_DELAY);
    *status = s_status;
    xSemaphoreGive(s_status_mutex);
}
