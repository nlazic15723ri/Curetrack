#pragma once

#include <stdbool.h>
#include "esp_err.h"

typedef enum {
    LED_GREEN = 0,
    LED_YELLOW = 1,
    LED_RED = 2,
    LED_COUNT = 3
} led_t;

esp_err_t leds_init(void);
void leds_set(led_t led, bool on);
void leds_only(led_t led);
void leds_all_off(void);
