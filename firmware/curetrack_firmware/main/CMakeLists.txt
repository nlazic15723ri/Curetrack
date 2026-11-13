idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES buzzer dht11 wifi_manager ds18b20 leds ssd1306 esp_driver_i2c nvs_flash esp_wifi esp_netif esp_event esp_driver_gpio)
