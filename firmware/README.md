# Firmware (ESP32-S3 + Arduino IDE)

## Setup

1. **ESP32 core** – Arduino IDE → *Boards Manager* → install **esp32 by Espressif Systems**
   (if it doesn't show up, add `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
   under *File → Preferences → Additional boards manager URLs*).
2. **Libraries** – *Library Manager* → install:
   - `PubSubClient` (Nick O'Leary)
   - `ArduinoJson` (Benoit Blanchon, v7.x)
   - `Adafruit BMP280 Library` (pulls in `Adafruit Unified Sensor` and `Adafruit BusIO`)
   - `BH1750` (Christopher Laws)
3. **Config** – copy `edge_sensor_hub/config.example.h` to `edge_sensor_hub/config.h`
   and fill in Wi-Fi and MQTT broker details. `config.h` is git-ignored.
4. Open `edge_sensor_hub/edge_sensor_hub.ino` and set under **Tools**:
   - **Board** → *ESP32S3 Dev Module*
   - **Flash Size** → *16MB* (for N16R8 boards)
   - **PSRAM** → *OPI PSRAM* (for N16R8 boards)
   - **USB CDC On Boot** → *Enabled* if you plug into the port labelled **USB**;
     leave *Disabled* if you use the port labelled **COM/UART**
   - **Port** → the board's COM port
5. **Upload**, then open the Serial Monitor at **115200 baud**.

> If the upload doesn't start, hold **BOOT**, tap **RST**, release **BOOT**, and upload again.

> Arduino IDE requires the sketch folder name to match the `.ino` file name —
> keep `edge_sensor_hub/edge_sensor_hub.ino` together.

## Wiring

| Sensor | Pin | ESP32-S3 |
|---|---|---|
| BMP280 | VCC / GND | 3V3 / GND |
| BMP280 | SDA / SCL | GPIO 8 / GPIO 9 |
| BH1750 | VCC / GND | 3V3 / GND |
| BH1750 | SDA / SCL | GPIO 8 / GPIO 9 (shared bus) |
| PIR | VCC / GND | 5V for HC-SR501, 3V3 for AM312 / GND |
| PIR | OUT | GPIO 4 |

Pins can be changed in `config.h`. On boot the Serial Monitor reports which
I2C addresses were found; a missing sensor is retried every sample and its
fields are simply left out of the payload until it responds.

## MQTT contract

| Topic | Payload | Retained |
|---|---|---|
| `edge-anomaly/<device-id>/readings` | JSON reading (below) | no |
| `edge-anomaly/<device-id>/status` | `online` / `offline` (LWT) | yes |

```json
{
  "device": "esp32-a1b2c3d4e5f6",
  "seq": 42,
  "uptime_ms": 210345,
  "sensors": {
    "temperature_c": 23.4,
    "pressure_hpa": 1013.2,
    "light_lux": 182.5,
    "motion": false,
    "motion_events": 2
  },
  "system":  { "rssi": -61, "free_heap": 231456 }
}
```

- `motion`: PIR output level at the moment of the sample.
- `motion_events`: rising edges counted since the previous message, so short
  movements between samples are not lost.

Quick test without the rest of the stack:

```bash
mosquitto_sub -h <broker-ip> -t "edge-anomaly/#" -v
```
