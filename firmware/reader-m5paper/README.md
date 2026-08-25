# reader-m5paper

Carl **reader node** on M5Stack [M5Paper](https://docs.m5stack.com/en/core/m5paper) — a 4.7" e-paper + touch dashboard for the kitchen counter. Premium tier alongside the iOS app: always-on glanceable view of every plant the hub knows about, no phone needed.

## Architecture

Thin Wi-Fi client of the Carl hub. Polls `http://carl-hub.local/api/nodes` (the same OpenAPI contract the iOS app codegens against) and renders to e-paper. The hub stays the single source of truth — it owns BLE scanning, per-node AES key storage, history, and the daily cloud upload to Project-Norman. **The reader holds no AES keys and does no decryption itself.**

```
[Sensor nodes × N]                     [Camera node × 1]
        |  BLE BTHome (encrypted)             |
        v                                     v
                  [Carl hub: ESP32]
                       |
                       | Wi-Fi LAN HTTP (mDNS: carl-hub.local)
        +--------------+--------------+-----------------+
        v              v              v                 v
  [phone browser]  [iOS app]   [M5Paper reader]   [Home Assistant]
```

If you want a no-hub deployment instead, the design memory captures a **v2 standalone-BLE-reader mode** — port the BTHome decoder into this build, scan advertisements directly, decrypt with shared keys. Out of scope for the v1 build here.

## Build

This target uses **PlatformIO + Arduino-ESP32** because the M5Paper's e-paper, touch, RTC, and onboard sensor support is canonical and well-tested through M5Stack's M5EPD library — a different toolchain choice from the Zephyr-based sensor node and ESP-IDF-based hub, but fitting for the device.

```bash
# PlatformIO CLI
cd firmware/reader-m5paper
pio run                     # build
pio run -t upload           # flash via USB-C
pio run -t monitor          # serial console at 115200
```

To bring up the firmware against fixtures (no hub, no Wi-Fi router), build with mock mode via the `PLATFORMIO_BUILD_FLAGS` env var (PlatformIO's `run -- <flag>` trailing-args form isn't supported by this project's pio version):

```bash
PLATFORMIO_BUILD_FLAGS="-DCARL_READER_USE_MOCK" pio run -e m5paper
```

The dashboard then renders against a hardcoded list of two plants (with a plausible 24h soil-decline history for the detail-page graph) from `hub_client.cpp`, useful for layout iteration on real hardware while the hub firmware is still being built.

## First-boot flow

1. Power on. The M5Paper boots, shows a splash with "Project Carl / Plant reader / Connecting…".
2. If no Wi-Fi credentials are stored, the device raises a captive-portal AP named **`Carl-Reader-Setup`**. Connect from your phone, pick your home Wi-Fi network, enter the password.
3. Reader joins the network, resolves `carl-hub.local` via mDNS, polls `/api/nodes`, and renders the card grid.
4. Refresh cadence: every 5 minutes (`CARL_READER_POLL_SEC`, build flag overridable). E-paper only repaints when readings have actually changed.

## Touch: tap a card for detail + history

Tap any plant card to open a full-screen detail page: current reading in full (soil %, severity, T/H/P/lux/battery), plus a line graph of its recent trend pulled from `GET /api/nodes/{id}/history?range=24h`. Tap **"< Back"** (top-left) to return to the grid.

- History is fetched **once**, on entering the detail page — not on every poll — so the graph doesn't trigger a full e-paper refresh every 10–60s while you're reading it. Tap back and forward again to refresh it.
- The graphed metric is picked automatically: soil % for a plant probe, falling back to temperature / humidity / illuminance for a node that doesn't report soil (e.g. a room node).
- Touch polling runs independently of the network poll cadence (checked every ~200ms) so taps feel responsive even though hub polling itself is slow.

## Files

- `platformio.ini` — board, libs, partitions
- `no_ota.csv` — partition table (no OTA — gives the binary 6 MB of headroom)
- `src/main.cpp` — boot, poll loop, repaint-on-change
- `src/wifi_setup.{h,cpp}` — first-boot captive portal via WiFiManager
- `src/hub_client.{h,cpp}` — `GET /api/nodes` + `GET /api/nodes/{id}/history` + JSON parsing; mock mode behind `CARL_READER_USE_MOCK`
- `src/dashboard.{h,cpp}` — e-paper rendering: header + card grid + status line + per-plant detail page with a history graph; touch hit-testing

## What's deferred

- Long-press → snooze alerts.
- Multi-column layout on the 540×960 panel (current build runs landscape 960×540; a portrait mode would need the grid math reworked).
- Battery-level reporting + light-sleep between polls (right now the loop just `delay`s; on USB power that's fine, on internal LiPo we want `esp_sleep_enable_timer_wakeup` for proper deep sleep).
- v2 standalone BLE reader mode (port the BTHome decoder, scan directly, no hub dependency).
