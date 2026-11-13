#include "ssd1306.h"
#include <stdlib.h>
#include <string.h>

#define TIMEOUT_MS 100

struct ssd1306 {
    i2c_master_dev_handle_t dev;
    uint8_t fb[SSD1306_WIDTH * SSD1306_HEIGHT / 8];   /* 1024 B, bit 0 = gornji red stranice */
};

/* Font 5x7: svaka kolona je jedan bajt, bit 0 = gornji red. Samo znakovi koji nam trebaju. */
static const uint8_t *glyph(char c)
{
    static const uint8_t blank[5] = {0, 0, 0, 0, 0};
    static const uint8_t g0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
    static const uint8_t g1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
    static const uint8_t g2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
    static const uint8_t g3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
    static const uint8_t g4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
    static const uint8_t g5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
    static const uint8_t g6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
    static const uint8_t g7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
    static const uint8_t g8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
    static const uint8_t g9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
    static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
    static const uint8_t minus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
    static const uint8_t pct[5] = {0x23, 0x13, 0x08, 0x64, 0x62};
    static const uint8_t gC[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
    static const uint8_t gT[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
    static const uint8_t gH[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
    static const uint8_t gE[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
    static const uint8_t gR[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
    static const uint8_t gM[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
    static const uint8_t gF[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
    static const uint8_t gW[5] = {0x3F, 0x40, 0x38, 0x40, 0x3F};
    static const uint8_t gI[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
    static const uint8_t gO[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
    static const uint8_t gK[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
    static const uint8_t gN[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
    switch (c) {
    case '0': return g0; case '1': return g1; case '2': return g2; case '3': return g3;
    case '4': return g4; case '5': return g5; case '6': return g6; case '7': return g7;
    case '8': return g8; case '9': return g9;
    case '.': return dot; case '-': return minus; case ':': return colon; case '%': return pct;
    case 'C': return gC; case 'T': return gT; case 'H': return gH; case 'E': return gE; case 'R': return gR;
    case 'M': return gM; case 'F': return gF; case 'W': return gW; case 'I': return gI;
    case 'O': return gO; case 'K': return gK; case 'N': return gN;
    default:  return blank;
    }
}

static esp_err_t send(struct ssd1306 *d, const uint8_t *buf, size_t len)
{
    return i2c_master_transmit(d->dev, buf, len, TIMEOUT_MS);
}

esp_err_t ssd1306_init(i2c_master_bus_handle_t bus, uint8_t addr, ssd1306_handle_t *out)
{
    struct ssd1306 *d = calloc(1, sizeof(*d));
    if (!d) {
        return ESP_ERR_NO_MEM;
    }
    i2c_device_config_t cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = 400000,
    };
    esp_err_t err = i2c_master_bus_add_device(bus, &cfg, &d->dev);
    if (err != ESP_OK) {
        free(d);
        return err;
    }
    /* prvi bajt 0x00 = kontrolni bajt "slede komande" */
    static const uint8_t init_seq[] = {
        0x00,
        0xAE,             /* ekran isključen */
        0xD5, 0x80,       /* takt */
        0xA8, 0x3F,       /* 64 reda */
        0xD3, 0x00,       /* bez pomeraja */
        0x40,             /* početna linija 0 */
        0x8D, 0x14,       /* uključi interni pretvarač napona (charge pump) */
        0x20, 0x00,       /* horizontalno adresiranje */
        0xA1,             /* ogledalo po koloni */
        0xC8,             /* ogledalo po redu */
        0xDA, 0x12,       /* raspored pinova (128x64) */
        0x81, 0xCF,       /* kontrast */
        0xD9, 0xF1,
        0xDB, 0x40,
        0xA4,             /* prikaži sadržaj memorije */
        0xA6,             /* normalan prikaz (nije invertovan) */
        0xAF,             /* ekran uključen */
    };
    err = send(d, init_seq, sizeof(init_seq));
    if (err != ESP_OK) {
        i2c_master_bus_rm_device(d->dev);
        free(d);
        return err;
    }
    *out = d;
    return ESP_OK;
}

void ssd1306_clear(ssd1306_handle_t d)
{
    memset(d->fb, 0, sizeof(d->fb));
}

static void set_pixel(ssd1306_handle_t d, int x, int y)
{
    if (x < 0 || x >= SSD1306_WIDTH || y < 0 || y >= SSD1306_HEIGHT) {
        return;
    }
    d->fb[(y / 8) * SSD1306_WIDTH + x] |= (uint8_t)(1 << (y % 8));
}

void ssd1306_draw_text(ssd1306_handle_t d, int x, int y, const char *s, int scale)
{
    for (; *s; s++) {
        const uint8_t *g = glyph(*s);
        for (int col = 0; col < 5; col++) {
            for (int row = 0; row < 7; row++) {
                if (g[col] & (1 << row)) {
                    for (int sx = 0; sx < scale; sx++) {
                        for (int sy = 0; sy < scale; sy++) {
                            set_pixel(d, x + col * scale + sx, y + row * scale + sy);
                        }
                    }
                }
            }
        }
        x += 6 * scale;           /* 5 kolona znaka + 1 razmak */
    }
}

esp_err_t ssd1306_flush(ssd1306_handle_t d)
{
    /* kolone 0..127, stranice 0..7 */
    static const uint8_t window[] = {0x00, 0x21, 0x00, 0x7F, 0x22, 0x00, 0x07};
    esp_err_t err = send(d, window, sizeof(window));
    if (err != ESP_OK) {
        return err;
    }
    uint8_t buf[1 + SSD1306_WIDTH];
    buf[0] = 0x40;                /* kontrolni bajt "slede podaci" */
    for (int page = 0; page < SSD1306_HEIGHT / 8; page++) {
        memcpy(&buf[1], &d->fb[page * SSD1306_WIDTH], SSD1306_WIDTH);
        err = send(d, buf, sizeof(buf));
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}
