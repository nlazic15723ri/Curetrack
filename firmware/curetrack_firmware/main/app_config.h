#pragma once

#define DEVICE_ID       "CT-07"
#define FW_VERSION      "0.4.0"
#define SEND_PERIOD_S   10
#define MQTT_URI        "mqtt://172.20.10.2:1883"
#define PIN_DS18B20     GPIO_NUM_4
#define PROBE_CHANNELS  3
#define T0_C            (-10.0f)
#define FC_A            (-41.7f)
#define FC_B            (17.75f)
#define MIN_VALID_TIME  1700000000
#define PIN_OLED_SDA    GPIO_NUM_8
#define PIN_OLED_SCL    GPIO_NUM_9
#define OLED_ADDR       0x3C
