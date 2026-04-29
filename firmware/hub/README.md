# hub

Generic ESP32 dev board running the Carl hub firmware, built on **ESP-IDF**. ESP32-S3-DevKitC-1 is the recommended target (PSRAM helps the rolling history buffer); a classic ESP32-WROOM works with a smaller buffer.

## What it does

- Scans BLE for Carl/BTHome advertisements, decrypts with AES-CCM using per-node keys.
- Maintains a per-node rolling history (24 h, in PSRAM if available).
- Serves a mobile-friendly web dashboard at `http://carl-hub.local/` over the home Wi-Fi.
- (Optional) Bridges sensor data to Home Assistant via MQTT discovery.
- (Optional) Posts a daily JSON digest + latest camera JPEG to a configured Project-Norman endpoint.

## Build

Requires ESP-IDF v5.x.

```bash
cd firmware/hub

# First time only
idf.py set-target esp32s3
idf.py reconfigure

# Build / flash / monitor
idf.py build
idf.py -p /dev/cu.usbmodem<...> flash monitor

# Web assets in data/ → LittleFS partition
idf.py littlefs-flash   # after at least one full flash
```

## First-boot flow

1. Power on. The hub creates a `Carl-Hub-Setup` Wi-Fi access point.
2. Connect from your phone, pick your home Wi-Fi network, enter the password.
3. The hub reboots and is reachable at `http://carl-hub.local/`.
4. Provision each sensor node's AES key from the dashboard.

## Files

- `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv` — ESP-IDF project skeleton.
- `main/CMakeLists.txt`, `main/main.c`, `main/idf_component.yml` — application component.
- `data/` — web dashboard SPA (added in Phase 3); flashed to the LittleFS partition.
- `firmware/common/` (sibling) provides BTHome v2 decoder + AES-CCM.
