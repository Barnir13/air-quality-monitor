#include <Arduino.h>
#include <Wire.h>
#include <SensirionI2cScd4x.h>
#include "Adafruit_SHT4x.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include "config.h"

#define SDA_PIN 8
#define SCL_PIN 9
#define LED_PIN 3
#define DEEP_SLEEP_MASODPERC 600

const char* mqttTopicSht = "senzor/sht41";
const char* mqttTopicScd = "senzor/scd40";

const float HOMERSEKLET_MIN = 22.0;
const float HOMERSEKLET_MAX = 28.0;
const float PARATARTALOM_MIN = 50.0;
const float PARATARTALOM_MAX = 70.0;
const int CO2_MAX = 1000;

SensirionI2cScd4x scd4x;
Adafruit_SHT4x sht4;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

void deepSleep() {
    esp_sleep_enable_timer_wakeup((uint64_t)DEEP_SLEEP_MASODPERC * 1000000ULL);
    esp_deep_sleep_start();
}

void wifiCsatlakozas() {
    WiFi.begin(WIFI_SSID, WIFI_JELSZO);
    Serial.print("WiFi csatlakozás");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" OK");
    delay(1000);
}

void idoBeallitas() {
    configTime(3600, 3600, "pool.ntp.org");
    Serial.print("Idő szinkronizálás");
    while (time(nullptr) < 1000000000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println(" OK");
}

void mqttCsatlakozas() {
    int probak = 0;
    while (!mqtt.connected() && probak < 3) {
        Serial.print("MQTT csatlakozás...");
        if (mqtt.connect("esp32-senzor-001")) {
            Serial.println(" OK");
        } else {
            Serial.print(" Hiba, rc=");
            Serial.println(mqtt.state());
            probak++;
            delay(3000);
        }
    }
    if (!mqtt.connected()) {
        Serial.println("MQTT nem elérhető, deep sleep...");
        deepSleep();
    }
}

void riasztasEllenorzes(float homerseklet, float paratartalom, int co2) {
    bool riasztas = false;

    if (homerseklet < HOMERSEKLET_MIN) {
        Serial.println("RIASZTÁS: Hőmérséklet túl alacsony!");
        riasztas = true;
    }
    if (homerseklet > HOMERSEKLET_MAX) {
        Serial.println("RIASZTÁS: Hőmérséklet túl magas!");
        riasztas = true;
    }
    if (paratartalom < PARATARTALOM_MIN) {
        Serial.println("RIASZTÁS: Páratartalom túl alacsony!");
        riasztas = true;
    }
    if (paratartalom > PARATARTALOM_MAX) {
        Serial.println("RIASZTÁS: Páratartalom túl magas!");
        riasztas = true;
    }
    if (co2 > CO2_MAX) {
        Serial.println("RIASZTÁS: CO2 túl magas!");
        riasztas = true;
    }

    if (!riasztas) {
        Serial.println("Minden érték normális tartományban.");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    Wire.begin(SDA_PIN, SCL_PIN);

    // SCD40 init
    scd4x.begin(Wire, SCD40_I2C_ADDR_62);
    scd4x.stopPeriodicMeasurement();
    delay(2000);
    scd4x.setAutomaticSelfCalibrationEnabled(false);
    scd4x.startPeriodicMeasurement();

    Serial.print("SCD40 várakozás");
    bool isDataReady = false;
    while (!isDataReady) {
        delay(1000);
        scd4x.getDataReadyStatus(isDataReady);
        Serial.print(".");
    }
    Serial.println(" OK");
    delay(200);

    // SHT41 init
    if (!sht4.begin()) {
        Serial.println("HIBA: SHT41 nem található!");
    }
    sht4.setPrecision(SHT4X_HIGH_PRECISION);
    sht4.setHeater(SHT4X_NO_HEATER);

    wifiCsatlakozas();
    idoBeallitas();
    mqtt.setServer(MQTT_SERVER, MQTT_PORT);
    mqttCsatlakozas();

    // Időbélyeg
    time_t most = time(nullptr);
    struct tm* t = localtime(&most);
    char idoBuf[30];
    strftime(idoBuf, sizeof(idoBuf), "%Y-%m-%d %H:%M:%S", t);

    // SHT41 mérés
    sensors_event_t humidity, temp;
    sht4.getEvent(&humidity, &temp);
    float homerseklet = temp.temperature;
    float paratartalom = humidity.relative_humidity;

    String payloadSht = "{\"temperature\":" + String(homerseklet, 2) + ",\"humidity\":" + String(paratartalom, 2) + "}";
    mqtt.publish(mqttTopicSht, payloadSht.c_str());

    Serial.print("--- Mérés: ");
    Serial.println(idoBuf);
    Serial.print("SHT41 | Hőmérséklet: ");
    Serial.print(homerseklet, 2);
    Serial.print(" °C | Páratartalom: ");
    Serial.print(paratartalom, 2);
    Serial.println(" %RH");

    // SCD40 mérés
    uint16_t co2;
    float scdTemp, scdHumidity;
    uint16_t error = scd4x.readMeasurement(co2, scdTemp, scdHumidity);
    if (error) {
        Serial.println("SCD40: Hiba olvasáskor, újrapróbálás...");
        delay(100);
        error = scd4x.readMeasurement(co2, scdTemp, scdHumidity);
    }

    if (!error) {
        String payloadScd = "{\"co2\":" + String(co2) + "}";
        mqtt.publish(mqttTopicScd, payloadScd.c_str());
        Serial.print("SCD40 | CO2: ");
        Serial.print(co2);
        Serial.println(" ppm");
    } else {
        Serial.println("SCD40: Hiba olvasáskor (másodszor is)!");
        co2 = 0;
    }

    riasztasEllenorzes(homerseklet, paratartalom, co2);

    mqtt.loop();
    delay(500);

    Serial.println("Deep sleep 10 percre...");
    deepSleep();
}

void loop() {
    // deep sleep miatt ide nem kerül semmi
}