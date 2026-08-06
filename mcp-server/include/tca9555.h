#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    i2c_master_dev_handle_t device;
} tca9555_t;

esp_err_t tca9555_init(
    tca9555_t *expander,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t frequency_hz
);

esp_err_t tca9555_set_output(tca9555_t *expander, uint8_t exio, bool level);

#ifdef __cplusplus
}
#endif
