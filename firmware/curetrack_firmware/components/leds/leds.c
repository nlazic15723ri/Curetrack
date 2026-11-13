#include "leds.h"
#include "driver/gpio.h"

static const gpio_num_t s_pins[LED_COUNT] = {GPIO_NUM_12, GPIO_NUM_13, GPIO_NUM_14};

esp_err_t leds_init(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        gpio_config_t config = {
            .pin_bit_mask = 1ULL << s_pins[i],
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        esp_err_t err = gpio_config(&config);
        if (err != ESP_OK) {
            return err;
        }
        gpio_set_level(s_pins[i], 0);
    }
    return ESP_OK;
}

void leds_set(led_t led, bool on)
{
    if (led < 0 || led >= LED_COUNT) {
        return;
    }
    gpio_set_level(s_pins[led], on ? 1 : 0);
}

void leds_all_off(void)
{
    for (int i = 0; i < LED_COUNT; i++) {
        gpio_set_level(s_pins[i], 0);
    }
}

void leds_only(led_t led)
{
    leds_all_off();
    leds_set(led, true);
}
