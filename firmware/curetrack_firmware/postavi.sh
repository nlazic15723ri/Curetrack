#!/bin/bash
set -e
cd "$(dirname "$0")"
for c in buzzer dht11 wifi_manager; do
    if [ ! -d "components/$c" ]; then
        echo "Nedostaje components/$c: iskopiraj taj folder iz projekta sa vezbi"
        exit 1
    fi
done
sed -i 's/GPIO_NUM_16/GPIO_NUM_10/; s/GPIO16/GPIO10/g' components/buzzer/buzzer.c
sed -i 's/GPIO_NUM_9\b/GPIO_NUM_11/' components/dht11/dht11.c
grep -n "define BUZZER_GPIO" components/buzzer/buzzer.c
grep -n "define DHT11_GPIO" components/dht11/dht11.c
echo "Pinovi su podeseni: buzzer GPIO10, DHT11 GPIO11"
