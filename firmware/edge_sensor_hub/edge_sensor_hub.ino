// Edge Anomaly Detection - ESP32 sensor hub
//
// Phase 1: read sensors and publish JSON readings over MQTT.
//
// Sensors:
//   BMP280  (I2C)  temperature + barometric pressure
//   BH1750  (I2C)  ambient light in lux
//   PIR     (GPIO) motion
//
// Board: "ESP32S3 Dev Module" (Tools > Board > esp32)
// Libraries: PubSubClient (Nick O'Leary), ArduinoJson (Benoit Blanchon, v7),
//            Adafruit BMP280 Library, BH1750 (Christopher Laws)

#include <WiFi.h>
#include <Wire.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Adafruit_BMP280.h>
#include <BH1750.h>

#include "config.h"  // copy config.example.h to config.h and fill in

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

Adafruit_BMP280 bmp(&Wire);
BH1750 lightMeter;
bool bmpOk = false;
bool lightOk = false;

// PIR: count rising edges in an ISR so short pulses between publishes
// are not missed.
volatile uint32_t motionEvents = 0;
portMUX_TYPE motionMux = portMUX_INITIALIZER_UNLOCKED;

String deviceId;
String topicReadings;
String topicStatus;

unsigned long lastPublishMs = 0;
uint32_t seq = 0;

struct Reading {
  float temperatureC;
  float pressureHpa;
  float lightLux;
  bool motionNow;          // PIR output level at sample time
  uint32_t motionEvents;   // rising edges since the previous sample
};

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------

void IRAM_ATTR onMotion() {
  portENTER_CRITICAL_ISR(&motionMux);
  motionEvents = motionEvents + 1;
  portEXIT_CRITICAL_ISR(&motionMux);
}

// BMP280 boards ship at 0x76 or 0x77, and some are actually BME280s
// (chip id 0x60) - both are register-compatible for temp/pressure.
bool beginBmp() {
  const uint8_t addrs[] = {0x76, 0x77};
  const uint8_t chipIds[] = {BMP280_CHIPID, 0x60};
  for (uint8_t addr : addrs) {
    for (uint8_t id : chipIds) {
      if (bmp.begin(addr, id)) {
        Serial.printf("[bmp280] found at 0x%02X (chip id 0x%02X)\n", addr, id);
        bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,
                        Adafruit_BMP280::SAMPLING_X2,    // temperature
                        Adafruit_BMP280::SAMPLING_X16,   // pressure
                        Adafruit_BMP280::FILTER_X16,
                        Adafruit_BMP280::STANDBY_MS_500);
        return true;
      }
    }
  }
  Serial.println("[bmp280] not found - check wiring");
  return false;
}

bool beginLight() {
  const uint8_t addrs[] = {0x23, 0x5C};  // ADDR pin low / high
  for (uint8_t addr : addrs) {
    if (lightMeter.begin(BH1750::CONTINUOUS_HIGH_RES_MODE, addr, &Wire)) {
      Serial.printf("[bh1750] found at 0x%02X\n", addr);
      return true;
    }
  }
  Serial.println("[bh1750] not found - check wiring");
  return false;
}

void setupSensors() {
  Wire.begin(I2C_SDA_PIN, I2C_SCL_PIN);
  bmpOk = beginBmp();
  lightOk = beginLight();

  pinMode(PIR_PIN, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIR_PIN), onMotion, RISING);
}

Reading readSensors() {
  Reading r;

  // Retry sensors that failed at boot (e.g. loose jumper fixed later).
  if (!bmpOk) bmpOk = beginBmp();
  if (!lightOk) lightOk = beginLight();

  r.temperatureC = bmpOk ? bmp.readTemperature() : NAN;
  r.pressureHpa = bmpOk ? bmp.readPressure() / 100.0f : NAN;

  float lux = lightOk ? lightMeter.readLightLevel() : NAN;
  r.lightLux = (lux >= 0) ? lux : NAN;  // library returns negative on error

  r.motionNow = digitalRead(PIR_PIN) == HIGH;
  portENTER_CRITICAL(&motionMux);
  r.motionEvents = motionEvents;
  motionEvents = 0;
  portEXIT_CRITICAL(&motionMux);

  return r;
}

// ---------------------------------------------------------------------------
// Connectivity
// ---------------------------------------------------------------------------

void connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return;

  Serial.printf("[wifi] connecting to %s", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(500);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, ip=%s rssi=%d\n",
                  WiFi.localIP().toString().c_str(), WiFi.RSSI());
  } else {
    Serial.println("[wifi] failed, will retry");
  }
}

void connectMqtt() {
  if (mqtt.connected() || WiFi.status() != WL_CONNECTED) return;

  Serial.printf("[mqtt] connecting to %s:%d ... ", MQTT_HOST, MQTT_PORT);
  // Last will: broker marks the device offline if it drops unexpectedly.
  bool ok = mqtt.connect(deviceId.c_str(), MQTT_USER, MQTT_PASSWORD,
                         topicStatus.c_str(), 1, true, "offline");
  if (ok) {
    Serial.println("ok");
    mqtt.publish(topicStatus.c_str(), "online", true);
  } else {
    Serial.printf("failed, state=%d\n", mqtt.state());
  }
}

// ---------------------------------------------------------------------------
// Publishing
// ---------------------------------------------------------------------------

void addIfValid(JsonObject obj, const char* key, float value) {
  if (!isnan(value)) obj[key] = value;
}

void publishReading(const Reading& r) {
  JsonDocument doc;
  doc["device"] = deviceId;
  doc["seq"] = seq++;
  doc["uptime_ms"] = millis();

  JsonObject s = doc["sensors"].to<JsonObject>();
  addIfValid(s, "temperature_c", r.temperatureC);
  addIfValid(s, "pressure_hpa", r.pressureHpa);
  addIfValid(s, "light_lux", r.lightLux);
  s["motion"] = r.motionNow;
  s["motion_events"] = r.motionEvents;

  JsonObject sys = doc["system"].to<JsonObject>();
  sys["rssi"] = WiFi.RSSI();
  sys["free_heap"] = ESP.getFreeHeap();

  char payload[384];
  size_t len = serializeJson(doc, payload, sizeof(payload));

  bool ok = mqtt.publish(topicReadings.c_str(), payload, len);
  Serial.printf("[pub] %s %s\n", ok ? "ok  " : "FAIL", payload);
}

// ---------------------------------------------------------------------------

void setup() {
  Serial.begin(115200);
  delay(200);

  uint64_t mac = ESP.getEfuseMac();
  char id[32];
  snprintf(id, sizeof(id), "esp32-%04x%08x",
           (uint16_t)(mac >> 32), (uint32_t)mac);
  deviceId = id;
  topicReadings = String(MQTT_TOPIC_PREFIX) + "/" + deviceId + "/readings";
  topicStatus = String(MQTT_TOPIC_PREFIX) + "/" + deviceId + "/status";

  Serial.printf("\n[boot] %s\n", deviceId.c_str());

  setupSensors();
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  connectWiFi();
  connectMqtt();
}

void loop() {
  connectWiFi();
  connectMqtt();
  mqtt.loop();

  unsigned long now = millis();
  if (now - lastPublishMs >= PUBLISH_INTERVAL_MS) {
    lastPublishMs = now;
    Reading r = readSensors();
    if (mqtt.connected()) {
      publishReading(r);
    } else {
      Serial.println("[pub] skipped, mqtt not connected");
    }
  }

  delay(10);
}
