#include "dht11.h"
#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define DHT11_GPIO              GPIO_NUM_11
#define DHT11_START_LOW_US      20000
#define DHT11_TIMEOUT_US        120
#define DHT11_BIT_THRESHOLD_US  45

static bool initialized = false;

static bool wait_for_level(int level, uint32_t timeout_us)
{
    int64_t start_time = esp_timer_get_time();

    while (gpio_get_level(DHT11_GPIO) != level) {
        if ((esp_timer_get_time() - start_time) >= timeout_us) {
            return false;
        }
    }

    return true;
}


esp_err_t dht11_init(void)
{
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << DHT11_GPIO,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };

    esp_err_t err = gpio_config(&config);

    if (err != ESP_OK) {
        return err;
    }

    gpio_set_level(DHT11_GPIO, 1);

    initialized = true;

    return ESP_OK;
}

static esp_err_t read_byte(uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t result = 0;

    for (int i = 0; i < 8; i++) {

        if (!wait_for_level(1, DHT11_TIMEOUT_US)) {
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_start = esp_timer_get_time();

        if (!wait_for_level(0, DHT11_TIMEOUT_US)) {
            return ESP_ERR_TIMEOUT;
        }

        int64_t high_duration =
            esp_timer_get_time() - high_start;

        result <<= 1;

        if (high_duration > DHT11_BIT_THRESHOLD_US) {
            result |= 1;
        }
    }

    *value = result;

    return ESP_OK;
}


static esp_err_t read_raw_data(uint8_t data[5])
{
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(DHT11_GPIO, 0);

    esp_rom_delay_us(DHT11_START_LOW_US);

    gpio_set_level(DHT11_GPIO, 1);
    gpio_set_direction(DHT11_GPIO, GPIO_MODE_INPUT);

    if (!wait_for_level(0, DHT11_TIMEOUT_US)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!wait_for_level(1, DHT11_TIMEOUT_US)) {
        return ESP_ERR_TIMEOUT;
    }

    if (!wait_for_level(0, DHT11_TIMEOUT_US)) {
        return ESP_ERR_TIMEOUT;
    }

    for (int i = 0; i < 5; i++) {
        esp_err_t err = read_byte(&data[i]);

        if (err != ESP_OK) {
            return err;
        }
    }

    return ESP_OK;
}


esp_err_t dht11_read(float *temperature, float *humidity)
{
    if (temperature == NULL || humidity == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t data[5] = {0};

    esp_err_t err = read_raw_data(data);

    if (err != ESP_OK) {
        return err;
    }

    uint8_t checksum =
        (uint8_t)(data[0] + data[1] + data[2] + data[3]);

    if (checksum != data[4]) {
        return ESP_ERR_INVALID_CRC;
    }

    *humidity =
        (float)data[0] + ((float)data[1] / 10.0f);

    *temperature =
        (float)data[2] + ((float)data[3] / 10.0f);

    return ESP_OK;
}