# node-sensor

Battery-powered BLE broadcast plant probe, built on **Zephyr / nRF Connect SDK v2.6.1** via west. XIAO nRF52840, capacitive soil probe always present; T/H/P (BME280) + lux (VEML7700) on the mid-tier/display profile.

## Profiles

- **Display** — XIAO nRF52840 + XIAO Expansion Board. OLED (welcome splash, live readings, provisioning QR), buzzer, user button (short-press wake, long-press re-show QR, boot-hold for soil calibration). Standalone-usable when the hub is offline.
- **Cheap** — bare XIAO nRF52840 (no expansion board). No screen — enrolled instead via a printed QR label (see [`tools/provision.py`](../../tools/README.md)). Local feedback comes from the onboard RGB LED (momentary blink per sample, never solid-on — battery-conscious) and a piezo buzzer on critical soil. Soil calibration is a built-in factory default (`CARL_SOIL_FACTORY_CAL`), no button needed; an opt-in serial `CAL DRY/WET/SAVE/SHOW` console (`CARL_DEV_CAL`) exists for bench recalibration.

Both profiles broadcast identical encrypted BTHome v2 advertisements and share the RGB/buzzer feedback code (`CARL_HAS_RGB`/`CARL_HAS_BUZZER` — independent of the display flag, not gated to the display profile).

## Build

```bash
source ../../tools/env-ncs.sh    # activates west — see tools/README.md

cd firmware/node-sensor

# Display profile
west build -p -b xiao_ble -d build -- -DEXTRA_CONF_FILE=prj_display.conf -DCONFIG_CARL_HAS_QR=y
cp build/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE   # double-tap reset first to mount it

# Cheap profile
west build -p -b xiao_ble -d build -- -DEXTRA_CONF_FILE=prj_cheap.conf
cp build/zephyr/zephyr.uf2 /Volumes/XIAO-SENSE

# Cheap profile + serial soil recalibration console
west build -p -b xiao_ble -d build -- -DEXTRA_CONF_FILE=prj_cheap.conf -DCONFIG_CARL_DEV_CAL=y

# Deep-sleep power path (System ON idle + BLE torn down between samples —
# NOT sys_poweroff, which can't wake on a timer on this chip; see the
# comment on CARL_DEEP_SLEEP in Kconfig for why). Not yet µA-validated on a
# Power Profiler — build-verified only.
west build -p -b xiao_ble -d build -- -DEXTRA_CONF_FILE=prj_cheap.conf -DCONFIG_CARL_DEEP_SLEEP=y
```

`-p` does a pristine rebuild — always use it when switching profiles/overlays.

Flashed via UF2 (Adafruit bootloader, double-tap reset to mount `XIAO-SENSE`) — no SWD/J-Link needed for normal firmware updates.

## Provisioning

On first boot the node generates a 16-byte AES key. It's surfaced three ways depending on profile:
- **Display profile**: OLED shows it as text, then as a scannable QR (`CARL://<12-hex-MAC>/<32-hex-key>`), and a `CARL-PROV mac=... key=...` line on serial (first boot only).
- **Cheap profile**: `CARL-PROV mac=... key=...` printed on **every** boot (no screen to show it otherwise) — capture with [`tools/provision.py --port ...`](../../tools/README.md) to generate a stick-on QR label.

Either way, hand the MAC + key to the hub: `POST /api/nodes` (see [firmware/hub/README.md](../hub/README.md)).

## Erase before flashing

If the board has stale state (old AES key, old calibration) you want gone entirely:

```bash
nrfjprog -e    # needs SWD — the XIAO Expansion Board or a soldered header
```

Not needed for routine reflashing — NVS persists across a normal UF2 flash.

## Files

- `CMakeLists.txt` — application sources; pulls in `firmware/common/`.
- `Kconfig` — `CARL_DISPLAY_PROFILE`/`CARL_CHEAP_PROFILE` choice, sample intervals, `CARL_HAS_RGB`/`CARL_HAS_BUZZER`, `CARL_SOIL_FACTORY_CAL`/`_DRY_ADC`/`_WET_ADC`, `CARL_DEV_CAL`, `CARL_DEEP_SLEEP`.
- `prj.conf` — common Zephyr / BLE / sensor / NVS / crypto config (shared by both profiles).
- `prj_display.conf`, `prj_cheap.conf` — profile overlays.
- `boards/xiao_ble.overlay` — devicetree: soil ADC (AIN0), VBAT divider (AIN7), BME280, VEML7700, OLED, buzzer, button.
- `west.yml` — pinned to nRF Connect SDK v2.6.1.
- `src/main.cpp` — boot + adaptive-cadence sample loop, deep-sleep BLE teardown.
- `src/sensors.{h,cpp}` — BME280/VEML7700/soil ADC/battery reads, factory + dev-mode soil calibration.
- `src/dev_cal.{h,cpp}` — opt-in serial calibration console.
- `src/calibration.{h,cpp}` — display-profile button-driven dry/wet calibration flow.
- `src/keystore.{h,cpp}` — AES key + BTHome counter, NVS-persisted.
- `src/bthome_emit.{h,cpp}` — BTHome v2 encode + broadcast.
- `src/thresholds.{h,cpp}` — soil severity classification (OK/Warning/Critical) driving sample cadence + buzzer/RGB/OLED.
- `src/ui/oled.{h,cpp}`, `src/ui/buzzer.{h,cpp}`, `src/ui/rgb.{h,cpp}`, `src/ui/button.{h,cpp}` — display-profile OLED (oled.cpp only), shared buzzer + RGB feedback (both profiles), user button.
