#include "wav_reader.h"

#include <stdbool.h>
#include <string.h>

#include "esp_log.h"

static const char *TAG = "wav_reader";

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t) data[0] |
           ((uint16_t) data[1] << 8);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t) data[0] |
           ((uint32_t) data[1] << 8) |
           ((uint32_t) data[2] << 16) |
           ((uint32_t) data[3] << 24);
}

static esp_err_t skip_chunk(FILE *file, uint32_t chunk_size)
{
    long skip = (long) chunk_size;
    if ((chunk_size & 1U) != 0U) {
        skip += 1;  // WAV chunks are word-aligned.
    }

    return fseek(file, skip, SEEK_CUR) == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t wav_reader_open(wav_reader_t *reader, const char *path)
{
    if (reader == NULL || path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(reader, 0, sizeof(*reader));
    reader->file = fopen(path, "rb");
    if (reader->file == NULL) {
        ESP_LOGE(TAG, "No se pudo abrir %s", path);
        return ESP_ERR_NOT_FOUND;
    }

    uint8_t riff_header[12];
    if (fread(riff_header, 1, sizeof(riff_header), reader->file) !=
        sizeof(riff_header)) {
        wav_reader_close(reader);
        return ESP_ERR_INVALID_SIZE;
    }

    if (memcmp(riff_header, "RIFF", 4) != 0 ||
        memcmp(riff_header + 8, "WAVE", 4) != 0) {
        ESP_LOGE(TAG, "%s no es WAV RIFF valido", path);
        wav_reader_close(reader);
        return ESP_ERR_INVALID_RESPONSE;
    }

    bool found_format = false;
    bool found_data = false;
    uint16_t audio_format = 0;

    while (!found_data) {
        uint8_t chunk_header[8];
        if (fread(chunk_header, 1, sizeof(chunk_header), reader->file) !=
            sizeof(chunk_header)) {
            break;
        }

        const uint32_t chunk_size = read_le32(chunk_header + 4);

        if (memcmp(chunk_header, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                ESP_LOGE(TAG, "Chunk fmt demasiado pequeno");
                wav_reader_close(reader);
                return ESP_ERR_INVALID_SIZE;
            }

            uint8_t format_data[16];
            if (fread(format_data, 1, sizeof(format_data), reader->file) !=
                sizeof(format_data)) {
                wav_reader_close(reader);
                return ESP_ERR_INVALID_SIZE;
            }

            audio_format = read_le16(format_data);
            reader->channels = read_le16(format_data + 2);
            reader->sample_rate = read_le32(format_data + 4);
            reader->bits_per_sample = read_le16(format_data + 14);
            found_format = true;

            const uint32_t remaining = chunk_size - sizeof(format_data);
            if (remaining > 0 && skip_chunk(reader->file, remaining) != ESP_OK) {
                wav_reader_close(reader);
                return ESP_FAIL;
            }
        } else if (memcmp(chunk_header, "data", 4) == 0) {
            reader->data_remaining = chunk_size;
            found_data = true;
        } else {
            if (skip_chunk(reader->file, chunk_size) != ESP_OK) {
                wav_reader_close(reader);
                return ESP_FAIL;
            }
        }
    }

    if (!found_format || !found_data) {
        ESP_LOGE(TAG, "Faltan chunks fmt o data en %s", path);
        wav_reader_close(reader);
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (audio_format != 1 ||
        reader->channels != 1 ||
        reader->sample_rate != 16000 ||
        reader->bits_per_sample != 16) {
        ESP_LOGE(
            TAG,
            "Formato no soportado en %s: PCM=%u canales=%u Hz=%u bits=%u",
            path,
            audio_format,
            reader->channels,
            (unsigned) reader->sample_rate,
            reader->bits_per_sample
        );
        wav_reader_close(reader);
        return ESP_ERR_NOT_SUPPORTED;
    }

    return ESP_OK;
}

size_t wav_reader_read(wav_reader_t *reader, void *buffer, size_t buffer_size)
{
    if (reader == NULL || reader->file == NULL ||
        buffer == NULL || buffer_size == 0 ||
        reader->data_remaining == 0) {
        return 0;
    }

    size_t requested = buffer_size;
    if (requested > reader->data_remaining) {
        requested = reader->data_remaining;
    }

    const size_t received = fread(buffer, 1, requested, reader->file);
    reader->data_remaining -= (uint32_t) received;
    return received;
}

void wav_reader_close(wav_reader_t *reader)
{
    if (reader == NULL) {
        return;
    }

    if (reader->file != NULL) {
        fclose(reader->file);
    }

    memset(reader, 0, sizeof(*reader));
}
