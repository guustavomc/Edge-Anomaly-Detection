// Copy this file to config.h (same folder) and fill in your values.
// config.h is git-ignored so credentials never get committed.
#pragma once

// Wi-Fi (ESP32 supports 2.4 GHz only)
#define WIFI_SSID      "your-ssid"
#define WIFI_PASSWORD  "your-password"

// MQTT broker (the machine running Mosquitto on your LAN)
#define MQTT_HOST      "192.168.0.10"
#define MQTT_PORT      1883
#define MQTT_USER      nullptr   // or "user"
#define MQTT_PASSWORD  nullptr   // or "password"

// Topics: <prefix>/<device-id>/readings and <prefix>/<device-id>/status
#define MQTT_TOPIC_PREFIX  "edge-anomaly"

// Sampling
#define PUBLISH_INTERVAL_MS  5000UL

// Pins
#define I2C_SDA_PIN  21   // BMP280 + BH1750 share the I2C bus
#define I2C_SCL_PIN  22
#define PIR_PIN      27   // PIR digital output
