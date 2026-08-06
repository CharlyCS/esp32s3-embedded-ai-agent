#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    FILE *file;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint32_t data_remaining;
} wav_reader_t;

esp_err_t wav_reader_open(wav_reader_t *reader, const char *path);
size_t wav_reader_read(wav_reader_t *reader, void *buffer, size_t buffer_size);
void wav_reader_close(wav_reader_t *reader);

#ifdef __cplusplus
}
#endif
