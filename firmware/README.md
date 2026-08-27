# firmware

Active Project-Carl firmware. Three targets plus a shared library — each on the vendor-native toolchain.

| Folder | Target | Toolchain |
|---|---|---|
| [common/](common/) | BTHome v2 encoder/decoder, AES-CCM helpers, sensor drivers | Plain C/C++ headers; included by Zephyr, ESP-IDF, and Arduino builds |
| [node-sensor/](node-sensor/) | XIAO nRF52840 plant probe (display + cheap profiles) | **Zephyr / nRF Connect SDK** (west) |
| [node-room/](node-room/) | Nordic Thingy:53 per-room ambient node (T/H/P/lux) | **Zephyr / nRF Connect SDK** (west) |
| [node-camera/](node-camera/) | XIAO ESP32-S3 Sense camera node (bare, no expansion board) | **ESP-IDF** (idf.py) |
| [hub/](hub/) | Generic ESP32 hub running BLE scanner + web dashboard + optional MQTT / cloud bridges | **ESP-IDF** (idf.py) |
| [reader-m5paper/](reader-m5paper/) | M5Stack M5Paper reader node — premium e-paper + touch dashboard | **PlatformIO + Arduino-ESP32** (M5EPD library) |

Wire format for everything: BTHome v2 + AES-CCM-128 encryption. See [common/bthome/](common/bthome/).

The legacy Thingy:53 firmware is parked under [`../application-thingy53/`](../application-thingy53/) as a reference.
