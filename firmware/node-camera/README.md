# node-camera

USB-powered XIAO ESP32-S3 Sense camera node, built on **ESP-IDF**. Bare board — uses the Sense's onboard OV2640 camera and microSD slot. No XIAO Expansion Board.

Acts as a regular sensor node (broadcasts encrypted BTHome v2 advertisements like the cheap node) **plus** a connectable BLE peripheral with a Plant Camera Service the hub uses to pull a daily plant-analysis JPEG.

## Build

Requires ESP-IDF v5.x (`. $IDF_PATH/export.sh` in your shell first).

```bash
cd firmware/node-camera

# First time only
idf.py set-target esp32s3
idf.py reconfigure   # pulls esp32-camera component from the registry

# Build / flash / monitor
idf.py build
idf.py -p /dev/cu.usbmodem<...> flash monitor
```

## Files

- `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv` — ESP-IDF project skeleton.
- `main/CMakeLists.txt`, `main/main.c`, `main/idf_component.yml` — application component.
- `firmware/common/` (sibling) provides BTHome v2 encoder, AES-CCM, sensor drivers.
