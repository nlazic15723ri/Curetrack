# CureTrack

Uređaj za praćenje očvršćavanja betona metodom zrelosti (Nurse-Saul, ASTM C1074). Predmet: Internet stvari.

Ploča ESP32-S3 meri temperaturu u betonu (3 sonde DS18B20) i temperaturu i vlažnost vazduha (DHT11), računa zrelost i procenu čvrstoće, prikazuje ih na OLED-u, signalizuje stanje diodama i buzzerom, i šalje JSON poruku preko MQTT-a na temu `curetrack/CT-07/sat`.

U ovom repozitorijumu je trenutno **hardver i firmver**. Server (Mosquitto, TimescaleDB, web stranica) je u izradi i nije ovde.

## Šta je na šemi

| Deo | Pin ploče | Napomena |
|---|---|---|
| OLED SSD1306 128x64, SDA | GPIO8 | I2C, adresa 0x3C |
| OLED SSD1306, SCL | GPIO9 | |
| Buzzer (S) | GPIO10 | uključuje se niskim nivoom |
| DHT11, DATA | GPIO11 | temperatura i vlažnost vazduha |
| LED zelena | GPIO12 | preko otpornika na GND |
| LED žuta | GPIO13 | preko otpornika na GND |
| LED crvena | GPIO14 | preko otpornika na GND |
| DS18B20 sonde, DATA (žuta žica) | GPIO4 | sve tri na istu žicu, 4,7 kΩ između DATA i 3V3 |
| Dugme START / ACK | GPIO16 / GPIO15 | planirano, još nije vezano |

Sve se napaja sa 3V3, uz zajednički GND. Sonde: crvena žica na 3V3, crna na GND, žuta na GPIO4.

## Kada šta svetli i pišti

| Dioda | Kada svetli |
|---|---|
| zelena | merenje je uspešno poslato na MQTT |
| žuta | merenje postoji, ali nije poslato (nema Wi-Fi-ja ili brokera) |
| crvena | nijedna temperatura nije očitana (ni sonde ni DHT11) |

Uvek svetli najviše jedna dioda.

Buzzer: jednom kratko (200 ms) pri pokretanju, kao potvrda da ploča radi. Zvučni alarm (npr. za ACK dugme) je planiran, još nije implementiran.

OLED prikazuje: `T` temperaturu, `H` vlažnost, `M` zrelost, `F` procenjenu čvrstoću i `WIFI OK` / `WIFI NE`. Kad nema nijednog očitavanja piše `ERR`.

## Pokretanje

Potrebno: ESP-IDF 6.1, ploča ESP32-S3 povezana USB-C kablom.

```bash
. ~/.espressif/v6.1/esp-idf/export.sh      # putanja zavisi od instalacije
cd firmware/curetrack_firmware

./postavi.sh                               # jednom: postavlja pinove buzzera (GPIO10) i DHT11 (GPIO11)
idf.py set-target esp32s3                  # jednom
idf.py menuconfig                          # upiši ime i lozinku Wi-Fi mreže (Wi-Fi manager)
idf.py build
idf.py flash monitor                       # izlaz iz monitora: Ctrl+]
```

Pre prevođenja proveri u `main/app_config.h`:
- `MQTT_URI`: adresa brokera (IP računara na kome radi Mosquitto)
- `SEND_PERIOD_S`: 10 s za test, 3600 s za pravi rad

Ime i lozinka Wi-Fi mreže upisuju se samo u `sdkconfig`, koji je u `.gitignore` i ne ide u repozitorijum.

U terminalu se svakih `SEND_PERIOD_S` sekundi ispiše red tipa:

```
T1=24.50 T2=24.50 T3=24.50 M1=3.21 fc=0.0 poslato=da
```

## Spisak fajlova

```
firmware/curetrack_firmware/
├── CMakeLists.txt            projekat
├── .gitignore                build/, sdkconfig, managed_components/ ...
├── postavi.sh                podešava pinove u drajverima sa vežbi
├── main/
│   ├── main.c                glavni program: merenje, zrelost, JSON, MQTT, OLED, diode
│   ├── app_config.h          podešavanja: ime uređaja, period, MQTT adresa, konstante
│   ├── CMakeLists.txt
│   └── idf_component.yml     preuzima espressif/mqtt
└── components/
    ├── ds18b20/              naš drajver za sonde (1-Wire)
    ├── leds/                 naš drajver za tri diode
    ├── ssd1306/              naš drajver za OLED (I2C)
    ├── buzzer/               drajver sa vežbi
    ├── dht11/                drajver sa vežbi
    └── wifi_manager/         drajver sa vežbi (Wi-Fi iz menuconfig-a)
```

## Poznata ograničenja

- Sonde i dugmad još nisu povezane; bez sondi se kao rezerva koristi temperatura iz DHT11.
- Zrelost se resetuje pri restartu ploče (nije sačuvana u NVS-u).
- Veza sa Wi-Fi hotspotom još nije potvrđena.
- Konstante krive čvrstoće (`FC_A`, `FC_B`) su ilustrativne, za beton C25/30.
