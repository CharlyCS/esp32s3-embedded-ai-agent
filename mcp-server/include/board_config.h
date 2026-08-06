#pragma once

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"

/*
 * Waveshare ESP32-S3-AUDIO-Board
 *
 * Official board mapping:
 *   I2C SDA       GPIO11
 *   I2C SCL       GPIO10
 *   I2S MCLK      GPIO12
 *   I2S BCLK      GPIO13
 *   I2S LRCK/WS   GPIO14
 *   ESP32 TX      GPIO16 -> ES8311 DSDIN (speaker)
 *   ES7210 RX     GPIO15 -> ESP32 (microphone, unused in this project)
 *   PA_EN         TCA9555 EXIO8
 */

#define BOARD_I2C_PORT              I2C_NUM_0
#define BOARD_I2C_SDA               GPIO_NUM_11
#define BOARD_I2C_SCL               GPIO_NUM_10
#define BOARD_I2C_FREQUENCY_HZ      400000

#define BOARD_I2S_PORT              I2S_NUM_0
#define BOARD_I2S_MCLK              GPIO_NUM_12
#define BOARD_I2S_BCLK              GPIO_NUM_13
#define BOARD_I2S_WS                GPIO_NUM_14
#define BOARD_I2S_DATA_OUT          GPIO_NUM_16
#define BOARD_I2S_DATA_IN           GPIO_NUM_15

#define BOARD_TCA9555_I2C_ADDRESS   0x20
#define BOARD_TCA9555_PA_EXIO       8
#define BOARD_PA_ACTIVE_LEVEL       1

#define BOARD_AUDIO_SAMPLE_RATE     16000
#define BOARD_AUDIO_BITS            16
#define BOARD_AUDIO_CHANNELS        1

#define BOARD_AUDIO_TASK_STACK      8192
#define BOARD_AUDIO_TASK_PRIORITY   5
#define BOARD_AUDIO_QUEUE_LENGTH    8
#define BOARD_AUDIO_CHUNK_BYTES     2048
