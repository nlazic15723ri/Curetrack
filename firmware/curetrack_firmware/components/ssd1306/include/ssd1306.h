#pragma once
#include <stdint.h>
#include "driver/i2c_master.h"
#include "esp_err.h"

#define SSD1306_WIDTH  128
#define SSD1306_HEIGHT 64

typedef struct ssd1306 *ssd1306_handle_t;

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus, uint8_t addr, ssd1306_handle_t *out);
void ssd1306_clear(ssd1306_handle_t d);
/* Piše tekst u memoriju (framebuffer). scale=2 daje slova 10x14 piksela. */
void ssd1306_draw_text(ssd1306_handle_t d, int x, int y, const char *s, int scale);
/* Šalje framebuffer na ekran. */
esp_err_t ssd1306_flush(ssd1306_handle_t d);
