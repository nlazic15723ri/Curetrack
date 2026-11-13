#include "ds18b20.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#define MAX_PROBES 4

static const char *TAG = "ds18b20";

static gpio_num_t s_pin;
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t s_rom[MAX_PROBES][8];
static int s_count = 0;
static uint8_t s_search_rom[8];
static int s_last_discrepancy;
static bool s_last_device;

esp_err_t ds18b20_init(gpio_num_t pin)
{
    s_pin = pin;
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << pin,
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&config);
    if (err != ESP_OK) {
        return err;
    }
    return gpio_set_level(pin, 1);
}

static bool ow_reset(void)
{
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(s_pin, 0);
    esp_rom_delay_us(480);
    gpio_set_level(s_pin, 1);
    esp_rom_delay_us(70);
    bool presence = (gpio_get_level(s_pin) == 0);
    portEXIT_CRITICAL(&s_mux);
    esp_rom_delay_us(410);
    return presence;
}

static void ow_write_bit(int bit)
{
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(s_pin, 0);
    if (bit) {
        esp_rom_delay_us(6);
        gpio_set_level(s_pin, 1);
        esp_rom_delay_us(64);
    } else {
        esp_rom_delay_us(60);
        gpio_set_level(s_pin, 1);
        esp_rom_delay_us(10);
    }
    portEXIT_CRITICAL(&s_mux);
}

static int ow_read_bit(void)
{
    portENTER_CRITICAL(&s_mux);
    gpio_set_level(s_pin, 0);
    esp_rom_delay_us(3);
    gpio_set_level(s_pin, 1);
    esp_rom_delay_us(9);
    int value = gpio_get_level(s_pin);
    esp_rom_delay_us(55);
    portEXIT_CRITICAL(&s_mux);
    return value;
}

static void ow_write_byte(uint8_t value)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(value & 1);
        value >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t value = 0;
    for (int i = 0; i < 8; i++) {
        value |= (uint8_t)(ow_read_bit() << i);
    }
    return value;
}

static uint8_t crc8(const uint8_t *data, int length)
{
    uint8_t crc = 0;
    for (int i = 0; i < length; i++) {
        uint8_t byte = data[i];
        for (int k = 0; k < 8; k++) {
            uint8_t mix = (crc ^ byte) & 1;
            crc >>= 1;
            if (mix) {
                crc ^= 0x8C;
            }
            byte >>= 1;
        }
    }
    return crc;
}

static bool search_next(void)
{
    int bit_number = 1;
    int last_zero = 0;
    int byte_number = 0;
    uint8_t mask = 1;
    bool found = false;

    if (s_last_device) {
        return false;
    }
    if (!ow_reset()) {
        s_last_discrepancy = 0;
        s_last_device = false;
        return false;
    }
    ow_write_byte(0xF0);

    do {
        int bit = ow_read_bit();
        int bit_complement = ow_read_bit();
        if (bit && bit_complement) {
            break;
        }
        int direction;
        if (bit != bit_complement) {
            direction = bit;
        } else {
            if (bit_number < s_last_discrepancy) {
                direction = ((s_search_rom[byte_number] & mask) > 0);
            } else {
                direction = (bit_number == s_last_discrepancy);
            }
            if (direction == 0) {
                last_zero = bit_number;
            }
        }
        if (direction) {
            s_search_rom[byte_number] |= mask;
        } else {
            s_search_rom[byte_number] &= (uint8_t)~mask;
        }
        ow_write_bit(direction);
        bit_number++;
        mask <<= 1;
        if (mask == 0) {
            byte_number++;
            mask = 1;
        }
    } while (byte_number < 8);

    if (bit_number > 64) {
        s_last_discrepancy = last_zero;
        if (s_last_discrepancy == 0) {
            s_last_device = true;
        }
        found = true;
    }
    if (!found || s_search_rom[0] == 0) {
        s_last_discrepancy = 0;
        s_last_device = false;
        found = false;
    }
    return found;
}

int ds18b20_scan(void)
{
    s_count = 0;
    s_last_discrepancy = 0;
    s_last_device = false;
    memset(s_search_rom, 0, sizeof(s_search_rom));

    while (s_count < MAX_PROBES && search_next()) {
        if (crc8(s_search_rom, 8) != 0) {
            ESP_LOGW(TAG, "adresa sonde ima los CRC, preskacem");
            continue;
        }
        memcpy(s_rom[s_count], s_search_rom, 8);
        ESP_LOGI(TAG, "sonda %d: %02X%02X%02X%02X%02X%02X%02X%02X", s_count + 1,
                 s_search_rom[0], s_search_rom[1], s_search_rom[2], s_search_rom[3],
                 s_search_rom[4], s_search_rom[5], s_search_rom[6], s_search_rom[7]);
        s_count++;
    }
    if (s_count == 0) {
        ESP_LOGW(TAG, "nema sondi, proveri zutu zicu i otpornik 4,7k");
    }
    return s_count;
}

int ds18b20_read(float *temps, int max)
{
    if (s_count == 0) {
        ds18b20_scan();
    }
    int total = (s_count < max) ? s_count : max;
    if (total == 0) {
        return 0;
    }

    for (int i = 0; i < total; i++) {
        temps[i] = NAN;
    }
    if (!ow_reset()) {
        ESP_LOGW(TAG, "sonde ne odgovaraju");
        return total;
    }
    ow_write_byte(0xCC);
    ow_write_byte(0x44);
    vTaskDelay(pdMS_TO_TICKS(800));

    for (int i = 0; i < total; i++) {
        if (!ow_reset()) {
            continue;
        }
        ow_write_byte(0x55);
        for (int k = 0; k < 8; k++) {
            ow_write_byte(s_rom[i][k]);
        }
        ow_write_byte(0xBE);
        uint8_t scratchpad[9];
        for (int k = 0; k < 9; k++) {
            scratchpad[k] = ow_read_byte();
        }
        if (crc8(scratchpad, 9) != 0) {
            ESP_LOGW(TAG, "sonda %d: los CRC", i + 1);
            continue;
        }
        int16_t raw = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
        temps[i] = raw / 16.0f;
    }
    return total;
}
