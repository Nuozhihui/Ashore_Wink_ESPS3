#pragma once

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "driver/i2c.h"

#define AHT20_ADDR 0x38
#define AHT20_TIMEOUT_MS 1000

void aht20_init(void);
void aht20_read_data(float *humidity, float *temperature);