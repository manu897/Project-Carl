# node-sensor

Battery-powered BLE broadcast sensor node, built on **Zephyr / nRF Connect SDK** via west.

Same pattern as the legacy [`application-thingy53/`](../../application-thingy53/): a single Zephyr application with two build configurations selected by Kconfig overlays.

## Profiles

- **Display** — XIAO nRF52840 + XIAO Expansion Board. OLED + buzzer + button. Standalone-usable.
- **Cheap** — bare XIAO nRF52840 (or future custom PCB). Broadcast-only; calibration via a connectable BLE window on first boot.

Both profiles broadcast identical encrypted BTHome v2 advertisements.

## Build

From the top of the Project-Carl checkout:

```bash
# (one-time) initialize west pointed at this manifest
python3 -m venv .venv && source .venv/bin/activate
pip install west
west init -l firmware/node-sensor
west update
source external/zephyr/zephyr-env.sh

# Display profile (XIAO Expansion Board)
west build -b xiao_ble firmware/node-sensor -- -DEXTRA_CONF_FILE=prj_display.conf
west flash

# Cheap profile (bare board)
west build -b xiao_ble firmware/node-sensor -p -- -DEXTRA_CONF_FILE=prj_cheap.conf
west flash
```

`-p` does a pristine rebuild — use it when switching profiles.

## Erase before flashing

If the board has stale state (e.g. old AES key), full-erase first:

```bash
nrfjprog -e
```

## Files

- `CMakeLists.txt` — application sources; pulls in `firmware/common/`.
- `Kconfig` — `CARL_DISPLAY_PROFILE` / `CARL_CHEAP_PROFILE` choice + sample interval.
- `prj.conf` — common Zephyr / BLE / sensor / NVS / crypto config.
- `prj_display.conf`, `prj_cheap.conf` — profile overlays.
- `boards/xiao_ble.overlay` — devicetree: soil ADC, BME280, VEML7700, OLED, buzzer, button.
- `west.yml` — pinned to nRF Connect SDK v2.6.1.
- `src/` — application code (created in Phase 2 of the plan).
