# node-room

Per-room ambient sensor node on the **Nordic Thingy:53**, built on **Zephyr / nRF Connect SDK v2.6.1** via west. Broadcasts temperature, humidity, pressure, and lux for a whole room — the hub joins each plant probe's soil reading with its assigned room's environment (see the hub's "room-join" feature). One per room, not one per plant — keeps BOM down at scale.

## What it does

- **Sensors**: onboard BME688 (T/H/P + gas resistance) and BH1749 (RGB+IR → photopic lux).
- **Battery**: real Li-Po voltage via the board's onboard `vbatt` divider (burst-read averaged — a single ADC acquisition undersamples this divider's high source impedance and reads low/jittery; see the comment in `sensors.cpp::readBatteryMv()`).
- **Air quality**: a self-learned rolling-baseline heuristic classifies gas resistance into good/moderate/poor. **Not broadcast over BTHome** — without Bosch's proprietary BSEC library there's no calibrated VOC value to send, and mislabeling a heuristic as BTHome's real-µg/m³ VOC object would misrepresent the data to Home Assistant. Used for local RGB feedback only.
- **RGB status LED + buzzer**: a boot-time rainbow sweep + Nokia chime, then per-sample feedback — green flash (all clear), amber (battery ≤15%, `CARL_ROOM_BATTERY_LOW_PCT`), purple (poor air quality), red + error beep (broadcast failure).
- **Provisioning**: MAC + AES key print to the USB CDC console on **every** boot (no screen on this board profile) — same `CARL://<MAC>/<KEY>`-compatible credentials the hub's `POST /api/nodes` expects.

## Build

```bash
source ../../tools/env-ncs.sh    # activates west — see tools/README.md

cd firmware/node-room
west build -p -b thingy53_nrf5340_cpuapp -d build
```

Flash via DFU (Thingy:53 ships with MCUboot, unlike node-sensor's UF2 bootloader):

```bash
# Hold the side button while plugging in USB, then:
nrfutil device program --firmware build/zephyr/dfu_application.zip --traits mcuBoot
```

`nrfutil device list` confirms it's in bootloader mode (shows `Bootloader Thingy:53`) before programming; `nrfutil install device` if the `device` subcommand isn't installed yet.

## Verify

```
*** Booting nRF Connect SDK v3.5.99-ncs1-1 ***
room-sensors: BME688 ready
room-sensors: BH1749 ready
room-led: RGB LED ready
room-buzzer: ready

============================================
room: MAC  D1:5C:BC:68:BE:C8
room: KEY  4c846ad393c0962060bcbcb60a6fa242
============================================

room sample #0: T=23.4C H=50% P=1001.5hPa lux=67 gas=12917167ohm(good) bat=82% (3940mV)
```

Hand the MAC + key to the hub, tag it as a room node, then tag plants sharing its room:

```bash
curl -X POST http://carl-hub.local/api/nodes \
  -H "Content-Type: application/json" \
  -d '{"mac":"D1:5C:BC:68:BE:C8","key_hex":"4c846ad393c0962060bcbcb60a6fa242","name":"Living Room"}'

curl -X PATCH http://carl-hub.local/api/nodes/node-BEC8 \
  -H "Content-Type: application/json" \
  -d '{"node_type":"room","room_id":"living-room"}'

# Then tag each plant in that room the same way:
curl -X PATCH http://carl-hub.local/api/nodes/node-XXXX \
  -H "Content-Type: application/json" \
  -d '{"room_id":"living-room"}'
```

## Follow-ups (not yet done)

- BME688 gas resistance → a real calibrated VOC/IAQ value needs Bosch's BSEC binary blob — currently just the local RGB heuristic (see above).
- No deep-sleep power path (unlike node-sensor's `CARL_DEEP_SLEEP`) — this board is typically USB-powered or a larger cell, so it hasn't been a priority.

## Files

- `CMakeLists.txt` — application sources; pulls in `firmware/common/`.
- `Kconfig` — `CARL_ROOM_SAMPLE_INTERVAL_SEC`, `CARL_ROOM_BATTERY_LOW_PCT`.
- `prj.conf` — BT broadcaster, BME688/BH1749 drivers, NVS, mbedTLS, C++17, float-printk.
- `boards/thingy53_nrf5340_cpuapp.overlay` — buzzer PWM alias + the SAADC channel config the board's `vbatt` divider needs (declared but not configured by the board's own DTS).
- `src/main.cpp` — boot animation, credential printing, sample loop, RGB/buzzer status feedback.
- `src/sensors.{h,cpp}` — BME688/BH1749 reads, real battery (burst-read), air-quality heuristic.
- `src/keystore.{h,cpp}` — AES key + BTHome counter, NVS-persisted (same pattern as node-sensor).
- `src/bthome_emit.{h,cpp}` — single-cycle BTHome v2 encode + broadcast (T/H/P/lux/battery fit in one legacy-AD frame — no rotation needed, unlike node-sensor).
- `src/ui/led.{h,cpp}` — RGB LED (boot sweep, momentary per-sample blinks — never solid-on).
- `src/ui/buzzer.{h,cpp}` — Nokia welcome chime + error beep.
