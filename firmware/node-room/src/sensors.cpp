#include "sensors.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/sys/printk.h>

namespace carl::room::sensors {

namespace {

// Both sensors are already declared in the Thingy:53 board DTS — we just
// grab the device handles by compatible string. BME688 uses Zephyr's
// bme680 driver (the chips are register-compatible on the I²C side).
const struct device* g_bme = DEVICE_DT_GET_ANY(bosch_bme680);
const struct device* g_bh  = DEVICE_DT_GET_ANY(rohm_bh1749);

uint16_t g_battery_mv = 0;

// Photopic-luminosity conversion for the BH1749. The chip outputs raw
// 16-bit RGB+IR counts; the green channel best matches the CIE V(λ)
// human-eye response. Rohm's app note gives an approximate constant of
// ~1 lux per 19 counts at gain=1×, integration=160 ms — the driver
// defaults. Plenty accurate for "is this plant getting bright/medium/low
// light" buckets.
constexpr float kBh1749GreenToLux = 1.0f / 19.0f;

}  // namespace

bool init() {
    bool any_ok = false;

    if (g_bme == nullptr) {
        printk("room-sensors: BME688 not present in devicetree\n");
    } else if (!device_is_ready(g_bme)) {
        printk("room-sensors: BME688 device not ready\n");
        g_bme = nullptr;
    } else {
        printk("room-sensors: BME688 ready\n");
        any_ok = true;
    }

    if (g_bh == nullptr) {
        printk("room-sensors: BH1749 not present in devicetree\n");
    } else if (!device_is_ready(g_bh)) {
        printk("room-sensors: BH1749 device not ready\n");
        g_bh = nullptr;
    } else {
        printk("room-sensors: BH1749 ready\n");
        any_ok = true;
    }

    return any_ok;
}

bool sample(Sample* out) {
    *out = Sample{};
    bool any_ok = false;

    if (g_bme != nullptr && sensor_sample_fetch(g_bme) == 0) {
        struct sensor_value v{};
        if (sensor_channel_get(g_bme, SENSOR_CHAN_AMBIENT_TEMP, &v) == 0) {
            out->temperature_c = sensor_value_to_double(&v);
            out->temp_ok = true; any_ok = true;
        }
        if (sensor_channel_get(g_bme, SENSOR_CHAN_HUMIDITY, &v) == 0) {
            out->humidity_pct = sensor_value_to_double(&v);
            out->humidity_ok = true;
        }
        if (sensor_channel_get(g_bme, SENSOR_CHAN_PRESS, &v) == 0) {
            // Zephyr returns kPa; BTHome wants hPa.
            out->pressure_hpa = sensor_value_to_double(&v) * 10.0;
            out->pressure_ok = true;
        }
        // Gas resistance is in ohms. Kept off the BTHome ad for v1 because
        // BTHome 0x13 wants ppb, not raw resistance — accurate conversion
        // needs Bosch's BSEC library which is a proprietary binary blob.
        if (sensor_channel_get(g_bme, SENSOR_CHAN_GAS_RES, &v) == 0) {
            out->gas_resistance_ohm = static_cast<uint32_t>(sensor_value_to_double(&v));
            out->gas_ok = true;
        }
    }

    if (g_bh != nullptr && sensor_sample_fetch(g_bh) == 0) {
        struct sensor_value g{};
        if (sensor_channel_get(g_bh, SENSOR_CHAN_GREEN, &g) == 0) {
            // sensor_value's val1 holds the integer green count (BH1749 raw).
            const float green_raw = static_cast<float>(g.val1);
            out->illuminance_lux = green_raw * kBh1749GreenToLux;
            out->lux_ok = true; any_ok = true;
        }
    }

    // TODO(node-room/battery): proper VBAT read via Thingy:53's vbatt
    // voltage-divider node (compatible="voltage-divider", io-channels =
    // <&adc 2>, power-gpios = <&gpio0 16 0>). Needs the adc_channel_cfg
    // input_positive set to NRF_SAADC_AIN2 (P0.04). Zephyr's battery sample
    // at samples/boards/nrf/battery shows the full pattern with calibrate +
    // oversampling. Skipped for v1 so we ship T/H/P/lux first and add the
    // battery monitor as a follow-up; the room node typically runs on USB
    // or its onboard cell with the official Nordic app's battery indicator,
    // so the BTHome `battery` field stays at 100 % for now.
    out->battery_pct = 100;

    return any_ok;
}

uint16_t batteryMv() { return g_battery_mv; }

}  // namespace carl::room::sensors
