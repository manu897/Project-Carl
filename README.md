# Project-Carl

**Project Carl** is named after the Swedish biologist *Carl Linnaeus* (Carl von Linné), widely acknowledged as the **"Father of Modern Botany."**

A home plant-monitoring system that scales from one plant to a houseful. Sensor nodes go in the soil, a room node covers ambient conditions, and a hub aggregates everything over BLE. The hub serves a phone-friendly dashboard on the local network. Home Assistant / HomePod integration is opt-in — the product works standalone.

## Architecture

```
[Plant probe × N]              [Room node × 1 per room]
 XIAO nRF52840                  Nordic Thingy:53
 capacitive soil probe          BME688 (T/H/P/gas)
 BME280 (T/H/P) — mid-tier     BH1749 (lux)
 VEML7700 (lux) — mid-tier     onboard LiPo or USB
 LiPo battery, deep-sleep      BLE non-connectable adv.
 BLE non-connectable adv.       (BTHome v2, AES-CCM)
 (BTHome v2, AES-CCM)
        \                              /
         \  BLE adv (BTHome)          /  BLE adv (BTHome)
          \                          /
           v                        v
        [Hub × 1 — ESP32-S3 dev board]
          BLE central · decrypts BTHome · per-node history
          HTTP server (mDNS: carl-hub.local)
          REST API for key provisioning + node data
          Optional MQTT bridge to Home Assistant
          Optional daily HTTPS POST to Project-Norman (cloud / ML)
                  |                            |
                  v                            v
   +---------------------------+    [Home Assistant → HomePod]
   |   Wi-Fi LAN HTTP readers  |
   +-------+-----------+-------+
           |           |
           v           v
   [phone /     [Project-Carl-IOS app]   [M5Paper reader (premium)]
    tablet                                always-on e-paper + touch
    browser]                              dashboard, kitchen-counter
                                          glanceable
```

### Product tiers

| Tier | Hardware | Sensors | ~BOM |
|---|---|---|---|
| **Budget plant probe** | XIAO nRF52840 (bare) | Soil moisture + battery | ~$21 |
| **Mid-tier plant probe** | XIAO nRF52840 + Expansion Board | Soil + light + OLED + buzzer + battery | ~$24 |
| **Room node** | Nordic Thingy:53 | Temp / humidity / pressure / lux + battery | ~$22 |
| **Camera node** | XIAO ESP32-S3 Sense | Same as budget + OV2640 camera | TBD |

All tiers share the same BTHome v2 + AES-CCM-128 BLE protocol. The hub decodes them identically.

## Repo layout

| Path | What lives there |
|---|---|
| [firmware/common/](firmware/common/) | BTHome v2 encoder/decoder + AES-CCM — shared by every node and the hub. |
| [firmware/node-sensor/](firmware/node-sensor/) | XIAO nRF52840 plant probe (display + cheap profiles). Zephyr / nRF Connect SDK v2.6.1. |
| [firmware/node-room/](firmware/node-room/) | Nordic Thingy:53 room node (T/H/P/lux). Zephyr / nRF Connect SDK v2.6.1. |
| [firmware/node-camera/](firmware/node-camera/) | XIAO ESP32-S3 Sense camera node. ESP-IDF. |
| [firmware/hub/](firmware/hub/) | ESP32-S3 hub firmware: BLE scanner + REST API + optional MQTT/cloud bridges. ESP-IDF. |
| [firmware/reader-m5paper/](firmware/reader-m5paper/) | M5Paper e-paper + touch dashboard. PlatformIO + Arduino-ESP32. |
| [documents/api/openapi.yaml](documents/api/openapi.yaml) | Authoritative OpenAPI spec — the contract iOS app + M5Paper reader compile against (iOS keeps a mirror). |
| [documents/](documents/) | Block diagrams, datasheets, API spec. |

## Build

Each firmware target uses its vendor-native toolchain.

**Plant probe** (Zephyr / nRF Connect SDK + west):
```bash
cd firmware/node-sensor
# Display profile (OLED + buzzer + button):
west build -p -b xiao_ble -d build -- -DEXTRA_CONF_FILE=prj_display.conf -DCONFIG_CARL_HAS_QR=y
# Flash via UF2: double-tap reset, then:
cp build/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE
```

**Room node** (Zephyr / nRF Connect SDK + west):
```bash
cd firmware/node-room
west build -p -b thingy53_nrf5340_cpuapp -d build
# Flash via DFU: hold side button while plugging USB, then:
nrfutil device program --firmware build/zephyr/dfu_application.zip --traits mcuBoot
```

**Hub** (ESP-IDF):
```bash
cd firmware/hub
idf.py set-target esp32s3
idf.py menuconfig   # set Wi-Fi SSID/password under "Carl Hub configuration"
idf.py build flash monitor
```

**M5Paper reader** (PlatformIO + Arduino):
```bash
cd firmware/reader-m5paper
pio run -t upload
# Mock mode (no hub needed):
pio run -e m5paper -- -DCARL_READER_USE_MOCK -t upload
```

## First-boot setup

1. **Flash a plant probe.** On first boot (display profile) the OLED shows a QR code containing the node's MAC + AES key. On the cheap profile, the key prints to the USB serial console.
2. **Flash the room node (optional).** The Thingy:53 prints its MAC + AES key to the USB CDC console on first boot. A rainbow LED sweep + Nokia chime confirm successful boot.
3. **Flash the hub.** Set your Wi-Fi credentials via `idf.py menuconfig` before flashing. The hub announces itself as `carl-hub.local` via mDNS.
4. **Provision nodes** by POSTing each node's credentials to the hub (name + calibration optional):
   ```bash
   # Plant probe
   curl -X POST http://carl-hub.local/api/nodes \
     -H "Content-Type: application/json" \
     -d '{"mac":"D0:B2:A9:AD:39:36","key_hex":"abcdef0123456789abcdef0123456789","name":"Bedroom Monstera"}'

   # Room node
   curl -X POST http://carl-hub.local/api/nodes \
     -H "Content-Type: application/json" \
     -d '{"mac":"D1:5C:BC:68:BE:C8","key_hex":"4c846ad393c0962060bcbcb60a6fa242","name":"Living Room"}'
   ```
   Names + calibration + keys persist in NVS across reboots. (The lightweight `POST /api/keys` with `{"mac","key"}` also works for quick bring-up, and menuconfig test-node entries seed a node without any HTTP call.)
5. **Verify** — within 30 seconds, `http://carl-hub.local/api/nodes` should return live readings.
6. **Open the dashboard** — point a browser at `http://carl-hub.local/` for the live plant grid (soil %, T/H/lux, battery, online status; polls every 10 s). The SPA is served from the hub's LittleFS partition and is flashed automatically with `idf.py flash` (no separate step).

## Hub REST API

| Method | Endpoint | Description |
|---|---|---|
| `GET` | `/api/health` | Hub id, version, uptime, node count, Wi-Fi RSSI |
| `GET` | `/api/nodes` | All known nodes with latest readings |
| `POST` | `/api/nodes` | Provision a node: `{"mac":..,"key_hex":..,"name":..,"calibration":..}` |
| `GET` | `/api/nodes/{id}` | One node's latest snapshot (404 if unknown) |
| `PATCH` | `/api/nodes/{id}` | Rename / set calibration thresholds |
| `DELETE` | `/api/nodes/{id}` | Remove node and purge its key |
| `GET` | `/api/nodes/{id}/history?range=24h\|7d\|30d` | Recent time-series readings |

Hub-internal convenience routes (handy for curl, not part of the client contract): `GET`/`POST` `/api/keys`, `DELETE /api/keys/{mac}`.

Timestamps are real ISO-8601 UTC once the hub syncs time via SNTP (a few seconds after Wi-Fi connects). History is held in RAM (~24h at the normal cadence); long-term history is owned by [Project-Norman](https://github.com/manu897/Project-Norman) via the periodic upload.

## Hardware

- **Hub** — any ESP32-S3 dev board (ESP32-S3-DevKitC-1 preferred for PSRAM).
- **Plant probe (display profile)** — [XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) + [XIAO Expansion Board](https://wiki.seeedstudio.com/Seeeduino-XIAO-Expansion-Board/) + [Grove capacitive moisture sensor](https://wiki.seeedstudio.com/Grove-Capacitive_Moisture_Sensor-Corrosion-Resistant/) + (mid-tier adds [BME280](https://www.bosch-sensortec.com/products/environmental-sensors/humidity-sensors-bme280/) + [VEML7700](https://www.vishay.com/en/product/84286/VEML7700)). Battery: EEMB LP603759 (1300 mAh LiPo).
- **Room node** — [Nordic Thingy:53](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-53) — onboard BME688 + BH1749 + RGB LED + piezo buzzer.
- **Reader (premium tier)** — [M5Stack M5Paper](https://docs.m5stack.com/en/core/m5paper) — 4.7" e-paper + touch + 1150 mAh internal LiPo.
- **Camera node** — [XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) (Phase 5, planned).

## Security

Every BLE advertisement is encrypted with AES-CCM-128 using a per-node key generated at first boot. The hub stores keys in NVS flash, the BTHome 4-byte counter prevents replay. The hub's REST API is LAN-only by default; cloud upload to Project-Norman is opt-in and uses a configured bearer token.

## Related projects

- [Project-Norman](https://github.com/manu897/Project-Norman) — cloud side. Owns ingestion, storage, ML for plant-health analysis, and long-term dashboards. The Carl-Norman boundary is MQTT (continuous) from the hub.
- [Project-Carl-IOS](https://github.com/manu897/Project-Carl-IOS) — native iOS app. Home + PlantDetail views running on fixtures; depends on the hub's OpenAPI spec.

## Author

[Manideep Reddy Tamma](mailto:manideep@bioliberty.co.uk) · [LinkedIn](https://www.linkedin.com/in/manideep-reddy-tamma/)

## References

- [BTHome v2 format](https://bthome.io/format/)
- [Home Assistant BTHome integration](https://www.home-assistant.io/integrations/bthome/)
- [BME280 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf)
- [BME688 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme688-ds000.pdf)
- [BH1749 datasheet](https://fscdn.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1749nuc-e.pdf)
- [nRF Connect SDK v2.6.1](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/2.6.1/nrf/index.html)
