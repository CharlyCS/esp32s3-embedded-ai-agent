#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool initialized;
    bool playing;
    int volume;
    char current_file[128];
    char last_error[128];
} audio_status_t;

esp_err_t audio_service_init(void);
esp_err_t audio_service_play(const char *path);
esp_err_t audio_service_stop(void);
esp_err_t audio_service_set_volume(int volume);
void audio_service_get_status(audio_status_t *status);

#ifdef __cplusplus
}
#endif
