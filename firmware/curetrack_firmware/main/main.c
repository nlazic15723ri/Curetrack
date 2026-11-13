#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_wifi.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "app_config.h"
#include "buzzer.h"
#include "dht11.h"
#include "ds18b20.h"
#include "leds.h"
#include "ssd1306.h"
#include "wifi_manager.h"

static const char *TAG = "curetrack";

static esp_mqtt_client_handle_t s_mqtt;
static volatile bool s_mqtt_ok = false;
static float s_maturity[PROBE_CHANNELS];
static int s_seq = 0;
static ssd1306_handle_t s_oled = NULL;

static void mqtt_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    switch ((esp_mqtt_event_id_t)id) {
    case MQTT_EVENT_CONNECTED:
        s_mqtt_ok = true;
        ESP_LOGI(TAG, "MQTT povezan sa brokerom %s", MQTT_URI);
        break;
    case MQTT_EVENT_DISCONNECTED:
        s_mqtt_ok = false;
        ESP_LOGW(TAG, "MQTT veza prekinuta");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGW(TAG, "MQTT greska, proveri adresu brokera");
        break;
    default:
        break;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t config = {
        .broker.address.uri = MQTT_URI,
    };
    s_mqtt = esp_mqtt_client_init(&config);
    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}

static void time_start(void)
{
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&config);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(20000)) == ESP_OK) {
        ESP_LOGI(TAG, "vreme podeseno preko SNTP");
    } else {
        ESP_LOGW(TAG, "SNTP nije podesio vreme");
    }
}

static bool make_timestamp(char *out, size_t size)
{
    time_t now;
    time(&now);
    if (now < MIN_VALID_TIME) {
        return false;
    }
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    size_t n = strftime(out, size, "%Y-%m-%dT%H:%M:%S", &tm_now);
    snprintf(out + n, size - n, "%s", tm_now.tm_isdst > 0 ? "+02:00" : "+01:00");
    return true;
}

static bool collect_channels(float channels[PROBE_CHANNELS], float air_t, bool air_ok)
{
    float probes[PROBE_CHANNELS];
    int found = ds18b20_read(probes, PROBE_CHANNELS);
    float first_valid = NAN;
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        channels[i] = (i < found) ? probes[i] : NAN;
        if (isnan(first_valid) && !isnan(channels[i])) {
            first_valid = channels[i];
        }
    }
    if (isnan(first_valid) && air_ok) {
        first_valid = air_t;
    }
    if (isnan(first_valid)) {
        return false;
    }
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        if (isnan(channels[i])) {
            channels[i] = first_valid;
        }
    }
    return true;
}

static float strength(void)
{
    float m_min = s_maturity[0];
    for (int i = 1; i < PROBE_CHANNELS; i++) {
        if (s_maturity[i] < m_min) {
            m_min = s_maturity[i];
        }
    }
    if (m_min <= 1.0f) {
        return 0.0f;
    }
    float fc = FC_A + FC_B * log10f(m_min);
    return fc > 0.0f ? fc : 0.0f;
}

static bool publish_message(const float channels[PROBE_CHANNELS], bool air_ok, float air_t, float air_h)
{
    char ts[40];
    if (!make_timestamp(ts, sizeof(ts))) {
        ESP_LOGW(TAG, "vreme jos nije podeseno, preskacem slanje");
        return false;
    }
    wifi_ap_record_t ap;
    int rssi = (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) ? ap.rssi : 0;
    char air[64];
    if (air_ok) {
        snprintf(air, sizeof(air), "{\"t\":%.1f,\"rh\":%.0f}", air_t, air_h);
    } else {
        snprintf(air, sizeof(air), "{\"t\":null,\"rh\":null}");
    }
    char msg[640];
    int n = snprintf(msg, sizeof(msg), "{\"dev\":\"%s\",\"ts\":\"%s\",\"seq\":%d,", DEVICE_ID, ts, ++s_seq);
    for (int i = 0; i < PROBE_CHANNELS; i++) {
        n += snprintf(msg + n, sizeof(msg) - n,
                      "\"s%d\":{\"min\":%.2f,\"max\":%.2f,\"avg\":%.2f,\"M\":%.2f},",
                      i + 1, channels[i], channels[i], channels[i], s_maturity[i]);
    }
    snprintf(msg + n, sizeof(msg) - n,
             "\"air\":%s,\"fc\":%.1f,\"alarm\":null,\"rssi\":%d,\"fw\":\"%s\"}",
             air, strength(), rssi, FW_VERSION);
    char topic[48];
    snprintf(topic, sizeof(topic), "curetrack/%s/sat", DEVICE_ID);
    int id = esp_mqtt_client_publish(s_mqtt, topic, msg, 0, 1, 0);
    return id >= 0;
}

static void display_update(float temp, bool air_ok, float humidity, float maturity, float fc, bool wifi_ok)
{
    if (s_oled == NULL) {
        return;
    }
    char line[24];
    ssd1306_clear(s_oled);
    snprintf(line, sizeof(line), "T %.1f C", temp);
    ssd1306_draw_text(s_oled, 0, 0, line, 2);
    if (air_ok) {
        snprintf(line, sizeof(line), "H %.0f %%", humidity);
    } else {
        snprintf(line, sizeof(line), "H -");
    }
    ssd1306_draw_text(s_oled, 0, 20, line, 2);
    snprintf(line, sizeof(line), "M %.2f F %.1f", maturity, fc);
    ssd1306_draw_text(s_oled, 0, 42, line, 1);
    ssd1306_draw_text(s_oled, 0, 54, wifi_ok ? "WIFI OK" : "WIFI NE", 1);
    ssd1306_flush(s_oled);
}

static void display_error(void)
{
    if (s_oled == NULL) {
        return;
    }
    ssd1306_clear(s_oled);
    ssd1306_draw_text(s_oled, 0, 20, "ERR", 3);
    ssd1306_flush(s_oled);
}

static void display_start(void)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = PIN_OLED_SDA,
        .scl_io_num = PIN_OLED_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    if (i2c_new_master_bus(&bus_config, &bus) != ESP_OK) {
        ESP_LOGE(TAG, "I2C magistrala nije pokrenuta");
        return;
    }
    if (i2c_master_probe(bus, OLED_ADDR, 100) != ESP_OK) {
        ESP_LOGW(TAG, "OLED nije pronadjen na adresi 0x%02X", OLED_ADDR);
        return;
    }
    if (ssd1306_init(bus, OLED_ADDR, &s_oled) != ESP_OK) {
        ESP_LOGE(TAG, "OLED nije pokrenut");
        s_oled = NULL;
        return;
    }
    ssd1306_clear(s_oled);
    ssd1306_draw_text(s_oled, 0, 0, "T 123 C", 2);
    ESP_LOGI(TAG, "test ekrana: %s", esp_err_to_name(ssd1306_flush(s_oled)));
    vTaskDelay(pdMS_TO_TICKS(3000));
    ESP_LOGI(TAG, "OLED pokrenut");
}

static void telemetry_task(void *arg)
{
    (void)arg;
    TickType_t last_wake = xTaskGetTickCount();
    while (true) {
        float air_t = 0.0f;
        float air_h = 0.0f;
        bool air_ok = (dht11_read(&air_t, &air_h) == ESP_OK);
        if (!air_ok) {
            ESP_LOGW(TAG, "DHT11 nije ocitan");
        }
        float channels[PROBE_CHANNELS];
        if (!collect_channels(channels, air_t, air_ok)) {
            leds_only(LED_RED);
            display_error();
            ESP_LOGE(TAG, "nema nijednog ocitavanja temperature");
        } else {
            for (int i = 0; i < PROBE_CHANNELS; i++) {
                s_maturity[i] += fmaxf(0.0f, channels[i] - T0_C) * SEND_PERIOD_S / 3600.0f;
            }
            bool sent = false;
            if (wifi_manager_is_connected() && s_mqtt_ok) {
                sent = publish_message(channels, air_ok, air_t, air_h);
            }
            leds_only(sent ? LED_GREEN : LED_YELLOW);
            display_update(channels[0], air_ok, air_h, s_maturity[0], strength(), wifi_manager_is_connected());
            ESP_LOGI(TAG, "T1=%.2f T2=%.2f T3=%.2f M1=%.2f fc=%.1f poslato=%s",
                     channels[0], channels[1], channels[2], s_maturity[0], strength(),
                     sent ? "da" : "ne");
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(SEND_PERIOD_S * 1000));
    }
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(buzzer_init());
    ESP_ERROR_CHECK(dht11_init());
    ESP_ERROR_CHECK(ds18b20_init(PIN_DS18B20));
    ESP_ERROR_CHECK(leds_init());
    display_start();

    buzzer_on();
    vTaskDelay(pdMS_TO_TICKS(200));
    buzzer_off();

    int probes = ds18b20_scan();
    ESP_LOGI(TAG, "pronadjeno sondi: %d", probes);
    xTaskCreate(telemetry_task, "telemetry", 6144, NULL, 5, NULL);

    bool wifi_started = (wifi_manager_init() == ESP_OK) && (wifi_manager_start() == ESP_OK);
    if (!wifi_started) {
        ESP_LOGE(TAG, "Wi-Fi nije pokrenut, upisi ime i lozinku u menuconfig");
    } else if (wifi_manager_wait_connected(30000) != ESP_OK) {
        ESP_LOGE(TAG, "Wi-Fi se nije povezao za 30 s, proveri ime i lozinku hotspota");
    }

    if (wifi_started) {
        time_start();
        mqtt_start();
    }

}
