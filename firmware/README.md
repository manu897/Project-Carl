# firmware

Active Project-Carl firmware. Three targets plus a shared library — each on the vendor-native toolchain.

| Folder | Target | Toolchain |
|---|---|---|
| [common/](common/) | BTHome v2 encoder/decoder, AES-CCM helpers, sensor drivers | Plain C/C++ headers; included by both Zephyr and ESP-IDF builds |
| [node-sensor/](node-sensor/) | XIAO nRF52840 sensor node (display + cheap profiles) | **Zephyr / nRF Connect SDK** (west) |
| [node-camera/](node-camera/) | XIAO ESP32-S3 Sense camera node (bare, no expansion board) | **ESP-IDF** (idf.py) |
| [hub/](hub/) | Generic ESP32 hub running BLE scanner + web dashboard + optional MQTT / cloud bridges | **ESP-IDF** (idf.py) |

Wire format for everything: BTHome v2 + AES-CCM-128 encryption. See [common/bthome/](common/bthome/).

The legacy Thingy:53 firmware is parked under [`../application-thingy53/`](../application-thingy53/) as a reference.
