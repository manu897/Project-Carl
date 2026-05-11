# hub

Generic ESP32 dev board running the Carl hub firmware, built on **ESP-IDF**. The shipped defaults target the **ESP32-DevKitC v4 with ESP-WROOM-32U** (4 MB flash, no PSRAM, classic Tensilica LX6 dual-core). An ESP32-S3-DevKitC-1 is also fine — `idf.py set-target esp32s3` and bump the partition table for 8 MB flash; just be aware the rolling history buffer wants the bigger heap, so the S3 path benefits from PSRAM if available.

## What it does

Phase 3a (current — shipped 2026-05-04):
- **Wi-Fi station** — connects to the home network using credentials from menuconfig.
- **mDNS** — advertises `<CONFIG_CARL_MDNS_HOSTNAME>.local` (default `carl-hub.local`) on the LAN, with the `_carl-hub._tcp` service record the iOS app and M5Paper reader use for discovery.
- **HTTP API** at port 80:
  - `GET /api/health` — uptime, build version, free heap.
  - `GET /api/nodes` — empty `[]` array (Phase 3b will populate from real BLE-decoded readings).

Phase 3b–3h queued (not yet built):
- BLE central scanner + AES-CCM decrypt of BTHome v2 advertisements.
- Per-node AES key store + `POST /api/keys` provisioning endpoint.
- Full `/api/nodes/{id}` + history endpoints.
- Web dashboard SPA in LittleFS.
- Captive-portal first-boot Wi-Fi flow (replaces hardcoded creds).
- Optional MQTT bridge to Home Assistant.
- Optional daily HTTPS POST to Project-Norman.

## Build

Requires ESP-IDF v5.x.

```bash
cd firmware/hub

# First time only — for the WROOM-32U dev board:
idf.py set-target esp32
# (or `idf.py set-target esp32s3` if you're on an S3-DevKitC; you'll also
#  want to bump partitions.csv from 4 MB to 8 MB layout)

# Set Wi-Fi credentials before flashing (Phase 3f will replace this with
# a first-boot captive portal — for now, edit them via menuconfig):
idf.py menuconfig
#   → Carl Hub configuration → Home Wi-Fi SSID
#   → Carl Hub configuration → Home Wi-Fi password

# Build / flash / monitor
idf.py build
idf.py -p /dev/cu.usbmodem<...> flash monitor
# On macOS, the WROOM-32U typically enumerates as /dev/cu.usbserial-0001
# or /dev/cu.SLAB_USBtoUART (depends on which USB-UART chip the dev board
# uses — CP2102 vs CH9102). `ls /dev/cu.*` after plugging in shows it.
```

## Verify

After flashing, you should see in the serial monitor:

```
I (NNNN) carl-hub: Project-Carl hub booting
I (NNNN) carl-wifi: joining <your-ssid>…
I (NNNN) carl-wifi: got ip 192.168.x.x
I (NNNN) carl-mdns: carl-hub.local advertised on _carl-hub._tcp:80
I (NNNN) carl-api: HTTP API listening on :80
I (NNNN) carl-hub: hub up — http://carl-hub.local/
```

Then from another machine on the same network:

```bash
curl http://carl-hub.local/api/health
# {"name":"carl-hub","version":"...","uptime_ms":1234,"free_heap_bytes":...}

curl http://carl-hub.local/api/nodes
# []
```

The empty array on `/api/nodes` is correct — Phase 3b adds the BLE scanner that populates it. With this build live, the **M5Paper reader can drop its `CARL_READER_USE_MOCK` flag** and point at the real hub; it'll render "No plants yet" until 3b lands.

## First-boot flow (current vs target)

**Right now (Phase 3a):** Wi-Fi creds are baked in at flash time via menuconfig. Fine for development; not a great friend-handoff story.

**After Phase 3f:** the hub creates a `Carl-Hub-Setup` SoftAP on first boot. You connect from your phone, open `192.168.4.1`, pick your home network from a list, enter the password. Saved to NVS. Reboot, joins your home Wi-Fi automatically.

## Files

- `CMakeLists.txt`, `sdkconfig.defaults`, `partitions.csv` — ESP-IDF project skeleton.
- `main/CMakeLists.txt` — source list + component requires.
- `main/Kconfig.projbuild` — Wi-Fi SSID/password/mDNS hostname/HTTP port settings.
- `main/main.c` — boot, init order, top-level orchestration.
- `main/wifi_sta.{c,h}` — station-mode Wi-Fi.
- `main/mdns_service.{c,h}` — mDNS advertisement.
- `main/http_api.{c,h}` — REST endpoints.
- `main/node_registry.{c,h}` — in-memory registry; empty in 3a, BLE-populated in 3b.
- `data/` — web dashboard SPA (added in Phase 3e).
- `firmware/common/` (sibling) provides BTHome v2 decoder + AES-CCM, used by Phase 3b.
