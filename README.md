# Project-Carl

**Project Carl** is named after the Swedish biologist *Carl Linnaeus* (Carl von Linné), widely acknowledged as the **"Father of Modern Botany."**

A home plant-monitoring system that scales from one plant to a houseful. Sensor nodes go in the soil. A hub aggregates them and serves a phone-friendly dashboard on the local network. Home Assistant / HomePod integration is opt-in — the product works standalone.

## Architecture

```
[Sensor node × N]              [Camera node × 1]
 XIAO nRF52840                  XIAO ESP32-S3 Sense (bare)
 BME280 (T/H/P)                 same sensors as cheap node
 capacitive soil probe          + OV2640 camera (daily still)
 VEML7700 lux (optional)        + onboard microSD slot
 coin cell, deep-sleep          USB powered
 BLE non-connectable adv.       BLE peripheral (BTHome adv.
 (BTHome v2, AES-CCM)            + connectable GATT for camera)
        \                              /
         \  BLE adv (BTHome)          /  BLE GATT (custom service)
          \                          /
           v                        v
        [Hub × 1 — generic ESP32 dev board]
          BLE central · decrypts BTHome · per-node history
          HTTP server (mDNS: carl-hub.local)
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

The sensor node has two product profiles built from the same firmware:

- **Cheap profile** — bare PCB, broadcast-only, no UI. Low BOM, mass-deployed.
- **Display profile** — XIAO nRF52840 + XIAO Expansion Board. OLED shows live readings, buzzer alerts on dry soil, button calibrates dry/wet. Standalone-usable when the hub is offline.

## Repo layout

| Path | What lives there |
|---|---|
| [firmware/common/](firmware/common/) | BTHome v2 encoder/decoder + AES-CCM + sensor drivers — shared by every node. |
| [firmware/node-sensor/](firmware/node-sensor/) | XIAO nRF52840 sensor node (display + cheap profiles). Zephyr / nRF Connect SDK. |
| [firmware/node-camera/](firmware/node-camera/) | XIAO ESP32-S3 Sense camera node (bare board, onboard SD). ESP-IDF. |
| [firmware/hub/](firmware/hub/) | Generic ESP32 hub firmware: BLE scanner + web dashboard + optional MQTT/cloud bridges. ESP-IDF. |
| [firmware/reader-m5paper/](firmware/reader-m5paper/) | **M5Stack M5Paper reader node — premium-tier always-on e-paper + touch dashboard.** PlatformIO + Arduino-ESP32. |
| [application-thingy53/](application-thingy53/) | Original Thingy:53 firmware — archived for reference. |
| [documents/](documents/) | Block diagrams, datasheets. |

## Build

Each firmware target uses its vendor-native toolchain — see the per-target README for full instructions.

**Sensor node** (Zephyr / nRF Connect SDK + west):
```bash
west build -b xiao_ble firmware/node-sensor -- -DEXTRA_CONF_FILE=prj_display.conf
west flash
# Cheap profile:
west build -b xiao_ble firmware/node-sensor -p -- -DEXTRA_CONF_FILE=prj_cheap.conf
```

**Camera node** (ESP-IDF):
```bash
cd firmware/node-camera
idf.py set-target esp32s3
idf.py build flash monitor
```

**Hub** (ESP-IDF):
```bash
cd firmware/hub
idf.py set-target esp32s3
idf.py build flash monitor
idf.py littlefs-flash    # web assets in data/
```

**M5Paper reader** (PlatformIO + Arduino):
```bash
cd firmware/reader-m5paper
pio run -t upload                                   # against the real hub
pio run -e m5paper -- -DCARL_READER_USE_MOCK -t upload   # mock fixtures, no hub needed
```

## First-boot setup

1. Flash one or more sensor nodes. On first boot each node generates a 16-byte AES key, prints it on the OLED (display profile) or surfaces it via a one-minute connectable BLE window (cheap profile).
2. Flash the hub. Connect a phone to the `Carl-Hub-Setup` access point and pick your home Wi-Fi.
3. Open `http://carl-hub.local/` and provision each node's key. Plants appear as cards; readings update each broadcast cycle.
4. (Optional) In the dashboard, configure an MQTT broker to push to Home Assistant, and/or a Norman endpoint URL for daily cloud upload.

## Hardware

- **Hub** — any generic ESP32 dev board (ESP32-S3-DevKitC-1 preferred for PSRAM).
- **Sensor node (display profile)** — [XIAO nRF52840](https://wiki.seeedstudio.com/XIAO_BLE/) + [XIAO Expansion Board](https://wiki.seeedstudio.com/Seeeduino-XIAO-Expansion-Board/) + [BME280](https://www.bosch-sensortec.com/products/environmental-sensors/humidity-sensors-bme280/) + [Grove capacitive moisture sensor](https://wiki.seeedstudio.com/Grove-Capacitive_Moisture_Sensor-Corrosion-Resistant/) + (optional) VEML7700.
- **Camera node** — [XIAO ESP32-S3 Sense](https://wiki.seeedstudio.com/xiao_esp32s3_getting_started/) + same sensor set.
- **Reader node (premium tier)** — [M5Stack M5Paper](https://docs.m5stack.com/en/core/m5paper) — 4.7" e-paper + touch + 1150 mAh internal LiPo. Optional add-on for always-on glanceable kitchen-counter dashboard.
- **Reference** — [Nordic Thingy:53](https://www.nordicsemi.com/Products/Development-hardware/Nordic-Thingy-53), original platform; see [application-thingy53/](application-thingy53/).

## Security

Every BLE advertisement is encrypted with AES-CCM-128 using a per-node key. The hub stores keys in NVS, the BTHome 4-byte counter prevents replay. The hub's web dashboard is LAN-only by default; cloud upload to Project-Norman is opt-in and uses a configured bearer token.

## Related projects

[Project-Norman](https://github.com/manu897/Project-Norman) — the data side. Owns cloud ingestion, storage, ML for plant-health analysis from camera stills, and long-term dashboards. The Carl ↔ Norman boundary is the daily HTTPS POST from the hub.

## Author

[Manideep Reddy Tamma](mailto:manideep@bioliberty.co.uk) · [LinkedIn](https://www.linkedin.com/in/manideep-reddy-tamma/)

## References

- [BTHome v2 format](https://bthome.io/format/)
- [Home Assistant BTHome integration](https://www.home-assistant.io/integrations/bthome/)
- [ESPHome Bluetooth Proxy](https://esphome.io/components/bluetooth_proxy.html) — alternative hub stack worth knowing about
- [BME280 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme280-ds002.pdf)
- [BME688 datasheet](https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme688-ds000.pdf)
