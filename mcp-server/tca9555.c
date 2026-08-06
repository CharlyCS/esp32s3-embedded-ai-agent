#include "tca9555.h"

#include <stddef.h>

#include "esp_log.h"

#define TCA9555_REG_OUTPUT_PORT_0  0x02
#define TCA9555_REG_OUTPUT_PORT_1  0x03
#define TCA9555_REG_CONFIG_PORT_0  0x06
#define TCA9555_REG_CONFIG_PORT_1  0x07

static const char *TAG = "tca9555";

static esp_err_t read_register(
    tca9555_t *expander,
    uint8_t reg,
    uint8_t *value
)
{
    if (expander == NULL || expander->device == NULL || value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_master_transmit_receive(
        expander->device,
        &reg,
        sizeof(reg),
        value,
        sizeof(*value),
        100
    );
}

static esp_err_t write_register(
    tca9555_t *expander,
    uint8_t reg,
    uint8_t value
)
{
    if (expander == NULL || expander->device == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t data[2] = {reg, value};
    return i2c_master_transmit(
        expander->device,
        data,
        sizeof(data),
        100
    );
}

esp_err_t tca9555_init(
    tca9555_t *expander,
    i2c_master_bus_handle_t bus,
    uint8_t address,
    uint32_t frequency_hz
)
{
    if (expander == NULL || bus == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = frequency_hz,
    };

    esp_err_t err = i2c_master_bus_add_device(
        bus,
        &device_config,
        &expander->device
    );

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "No se pudo agregar TCA9555 en 0x%02X: %s",
                 address, esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "TCA9555 inicializado en 0x%02X", address);
    return ESP_OK;
}

esp_err_t tca9555_set_output(tca9555_t *expander, uint8_t exio, bool level)
{
    if (expander == NULL || expander->device == NULL || exio > 15) {
        return ESP_ERR_INVALID_ARG;
    }

    const bool port_1 = exio >= 8;
    const uint8_t bit = (uint8_t) (exio % 8);
    const uint8_t mask = (uint8_t) (1U << bit);
    const uint8_t output_register =
        port_1 ? TCA9555_REG_OUTPUT_PORT_1 : TCA9555_REG_OUTPUT_PORT_0;
    const uint8_t config_register =
        port_1 ? TCA9555_REG_CONFIG_PORT_1 : TCA9555_REG_CONFIG_PORT_0;

    uint8_t output_value = 0;
    uint8_t config_value = 0;

    esp_err_t err = read_register(expander, output_register, &output_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo leyendo salida EXIO%u: %s",
                 exio, esp_err_to_name(err));
        return err;
    }

    err = read_register(expander, config_register, &config_value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Fallo leyendo configuracion EXIO%u: %s",
                 exio, esp_err_to_name(err));
        return err;
    }

    if (level) {
        output_value |= mask;
    } else {
        output_value &= (uint8_t) ~mask;
    }

    /*
     * Write the desired level before changing the pin to output, reducing
     * unwanted pulses on the amplifier enable line.
     */
    err = write_register(expander, output_register, output_value);
    if (err != ESP_OK) {
        return err;
    }

    config_value &= (uint8_t) ~mask;  // 0 = output
    return write_register(expander, config_register, config_value);
}
