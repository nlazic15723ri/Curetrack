#include "wifi_manager.h"

#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"

#define WIFI_CONNECTED_BIT BIT0

static const char *TAG = "WIFI_MANAGER";

static EventGroupHandle_t wifi_event_group;

static bool initialized = false;
static bool started = false;

static esp_netif_t *wifi_netif = NULL;

static void wifi_event_handler(
    void *arg,
    esp_event_base_t event_base,
    int32_t event_id,
    void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base == WIFI_EVENT &&
        event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Wi-Fi je pokrenut; povezivanje...");

        esp_err_t err = esp_wifi_connect();

        if (err != ESP_OK)
        {
            ESP_LOGW(TAG, "Pocetni zahtev za povezivanje nije uspeo: %s",
                     esp_err_to_name(err));
        }
    }
    else if (event_base == WIFI_EVENT &&
             event_id == WIFI_EVENT_STA_DISCONNECTED)
    {

        xEventGroupClearBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT);

        ESP_LOGW(TAG, "Wi-Fi je diskonektovan");

        if (started)
        {
            esp_err_t err = esp_wifi_connect();

            if (err != ESP_OK)
            {
                ESP_LOGW(TAG, "Ponovno povezivanje nije uspelo: %s",
                         esp_err_to_name(err));
            }
        }
    }
    else if (event_base == IP_EVENT &&
             event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event =
            (ip_event_got_ip_t *)event_data;

        ESP_LOGI(
            TAG,
            "Dobijena IP adresa: " IPSTR,
            IP2STR(&event->ip_info.ip));

        xEventGroupSetBits(
            wifi_event_group,
            WIFI_CONNECTED_BIT);
    }
}

esp_err_t wifi_manager_init(void)
{
    if (initialized)
    {
        return ESP_OK;
    }

    esp_err_t err = esp_netif_init();

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_event_loop_create_default();

    if (err != ESP_OK)
    {
        return err;
    }

    wifi_netif = esp_netif_create_default_wifi_sta();

    if (wifi_netif == NULL)
    {
        return ESP_FAIL;
    }

    wifi_event_group = xEventGroupCreate();

    if (wifi_event_group == NULL)
    {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t wifi_config = WIFI_INIT_CONFIG_DEFAULT();

    err = esp_wifi_init(&wifi_config);

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_event_handler_register(
        WIFI_EVENT,
        ESP_EVENT_ANY_ID,
        wifi_event_handler,
        NULL);

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_event_handler_register(
        IP_EVENT,
        IP_EVENT_STA_GOT_IP,
        wifi_event_handler,
        NULL);

    if (err != ESP_OK)
    {
        return err;
    }

    wifi_config_t sta_config = {0};

    if (strlen(CONFIG_WIFI_MANAGER_SSID) == 0)
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(CONFIG_WIFI_MANAGER_SSID) >=
        sizeof(sta_config.sta.ssid))
    {
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(CONFIG_WIFI_MANAGER_PASSWORD) >=
        sizeof(sta_config.sta.password))
    {
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(
        sta_config.sta.ssid,
        CONFIG_WIFI_MANAGER_SSID,
        strlen(CONFIG_WIFI_MANAGER_SSID));

    memcpy(
        sta_config.sta.password,
        CONFIG_WIFI_MANAGER_PASSWORD,
        strlen(CONFIG_WIFI_MANAGER_PASSWORD));

    sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;

    if (strlen(CONFIG_WIFI_MANAGER_PASSWORD) > 0)
    {
        sta_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    }

    err = esp_wifi_set_mode(WIFI_MODE_STA);

    if (err != ESP_OK)
    {
        return err;
    }

    err = esp_wifi_set_config(
        WIFI_IF_STA,
        &sta_config);

    if (err != ESP_OK)
    {
        return err;
    }

    initialized = true;

    ESP_LOGI(TAG, "Wi-Fi komponenta je inicijalizovana");

    return ESP_OK;
}

esp_err_t wifi_manager_start(void)
{
    if (!initialized)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (started)
    {
        return ESP_OK;
    }

    started = true;

    esp_err_t err = esp_wifi_start();

    if (err != ESP_OK)
    {
        started = false;
        return err;
    }

    ESP_LOGI(TAG, "Wi-Fi pokrenut");

    return ESP_OK;
}

bool wifi_manager_is_connected(void)
{
    if (wifi_event_group == NULL)
    {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(wifi_event_group);

    return (bits & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_manager_wait_connected(uint32_t timeout_ms)
{
    if (!initialized || !started)
    {
        return ESP_ERR_INVALID_STATE;
    }

    TickType_t timeout_ticks;

    if (timeout_ms == 0)
    {
        timeout_ticks = portMAX_DELAY;
    }
    else
    {
        timeout_ticks = pdMS_TO_TICKS(timeout_ms);

        if (timeout_ticks == 0)
        {
            timeout_ticks = 1;
        }
    }

    EventBits_t bits = xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        timeout_ticks);

    if ((bits & WIFI_CONNECTED_BIT) != 0)
    {
        return ESP_OK;
    }

    return ESP_ERR_TIMEOUT;
}