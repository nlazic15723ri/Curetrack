#pragma once

#include "driver/gpio.h"
#include "esp_err.h"

esp_err_t ds18b20_init(gpio_num_t pin);
int ds18b20_scan(void);
int ds18b20_read(float *temps, int max);
