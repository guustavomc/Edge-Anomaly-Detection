# Infrastructure

Docker Compose stack for the backend services. For now it runs the
**Mosquitto** MQTT broker; InfluxDB is added next.

## Start the broker

Requires Docker Desktop (running). From the repo root:

```bash
docker compose -f infra/docker-compose.yml up -d
```

Check it is up and see connections as they happen:

```bash
docker compose -f infra/docker-compose.yml logs -f mosquitto
```

Stop it:

```bash
docker compose -f infra/docker-compose.yml down
```

### "port is already allocated"

If `up` fails with `Bind for 0.0.0.0:1883 failed: port is already allocated`,
another broker is already using port 1883, for example the
Home-Monitoring-Hub stack, which restarts with Docker Desktop. Find it:

```bash
docker ps --format "{{.Names}}\t{{.Ports}}"
```

Stop that stack (its containers and data are kept), then run `up` again:

```bash
docker compose -p home-monitoring-hub-backend stop
```

Only one broker can listen on 1883 at a time. To switch back later, stop this
stack with `down` and run `docker compose start` in the other project.

## Point the ESP32 at the broker

1. Find this PC's LAN IP (Windows: `ipconfig` → *IPv4 Address* of the Wi-Fi/Ethernet adapter, e.g. `192.168.0.15`).
2. In `firmware/edge_sensor_hub/config.h` set `MQTT_HOST` to that IP and re-upload.
3. The first time, Windows may ask to allow Docker through the firewall — allow it on **private networks**. If the ESP32 still gets `state=-2`, add an inbound rule for TCP port 1883.

When the ESP32 connects, the Mosquitto log shows a line like
`New client connected from 192.168.0.42 as esp32-xxxxxxxxxxxx`.

## Watch the messages

Subscribe to everything the devices publish, using the client inside the container:

```bash
docker exec -it edge-mosquitto mosquitto_sub -t "edge-anomaly/#" -v
```

Topics used by the firmware:

| Topic | Payload |
|---|---|
| `edge-anomaly/<device-id>/readings` | JSON reading every 5 s |
| `edge-anomaly/<device-id>/status` | `online` / `offline` (retained, last will) |

See [firmware/README.md](../firmware/README.md#mqtt-contract) for the payload format.

## Security note

The broker accepts anonymous connections. That is fine on a home LAN during
development; before exposing it anywhere else, add a `password_file` in
`mosquitto/config/mosquitto.conf` and set `MQTT_USER` / `MQTT_PASSWORD` in the firmware config.
