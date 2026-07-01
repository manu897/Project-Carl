#include "sensors.h"

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/adc/voltage_divider.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace carl::room::sensors {

namespace {

// Both sensors are already declared in the Thingy:53 board DTS — we just
// grab the device handles by compatible string. BME688 uses Zephyr's
// bme680 driver (the chips are register-compatible on the I²C side).
const struct device* g_bme = DEVICE_DT_GET_ANY(bosch_bme680);
const struct device* g_bh  = DEVICE_DT_GET_ANY(rohm_bh1749);

// Onboard Li-Po monitor: the board's `vbatt` voltage-divider node (1.5M/180k
// on AIN2, gated by P0.16). VOLTAGE_DIVIDER_DT_SPEC_GET pulls the ADC channel
// + the divider ratio straight from devicetree, so the scaling math comes
// from the DTS rather than hardcoded constants.
const struct voltage_divider_dt_spec g_vbatt =
    VOLTAGE_DIVIDER_DT_SPEC_GET(DT_PATH(vbatt));
const struct gpio_dt_spec g_vbatt_power =
    GPIO_DT_SPEC_GET(DT_PATH(vbatt), power_gpios);

uint16_t g_battery_mv = 0;

// Read the onboard Li-Po in millivolts. Enables the divider, samples AIN2,
// scales by the DTS divider ratio, then disables the divider to stop the
// ~25uA leakage. Returns 0 on failure.
uint16_t readBatteryMv() {
    if (!adc_is_ready_dt(&g_vbatt.port)) return 0;
    if (adc_channel_setup_dt(&g_vbatt.port) != 0) return 0;

    // Enable the divider (power-gpios) and let the high-impedance node settle.
    if (gpio_is_ready_dt(&g_vbatt_power)) {
        gpio_pin_configure_dt(&g_vbatt_power, GPIO_OUTPUT_ACTIVE);
        k_msleep(2);
    }

    int16_t buf = 0;
    struct adc_sequence seq = {};
    (void)adc_sequence_init_dt(&g_vbatt.port, &seq);
    seq.buffer = &buf;
    seq.buffer_size = sizeof(buf);
    seq.calibrate = true;

    // High source impedance: a single acquisition undersamples (reads low and
    // jitters). Burst-read so the sample-and-hold converges, drop the first 8
    // warm-up reads, then average the settled tail. Same fix as the XIAO probe.
    uint16_t out_mv = 0;
    int32_t acc = 0;
    int n = 0;
    bool read_ok = true;
    for (int i = 0; i < 16; ++i) {
        if (adc_read(g_vbatt.port.dev, &seq) != 0) { read_ok = false; break; }
        if (i >= 8) { acc += buf; ++n; }
    }
    if (read_ok && n > 0) {
        int32_t mv = acc / n;
        if (adc_raw_to_millivolts_dt(&g_vbatt.port, &mv) == 0
            && voltage_divider_scale_dt(&g_vbatt, &mv) == 0
            && mv > 2000 && mv < 5000) {
            out_mv = static_cast<uint16_t>(mv);
        }
    }

    if (gpio_is_ready_dt(&g_vbatt_power)) {
        gpio_pin_configure_dt(&g_vbatt_power, GPIO_OUTPUT_INACTIVE);
    }
    return out_mv;
}

// LiPo discharge curve, linear over the useful 3.30 V (empty) → 4.20 V (full)
// span — same approximation the XIAO plant probe uses.
uint8_t batteryPctFromMv(uint16_t mv) {
    if (mv == 0)    return 0;
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return static_cast<uint8_t>((mv - 3300) * 100u / 900u);
}

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

    // Real onboard Li-Po reading via the board's vbatt divider. Falls back to
    // 0 % if the ADC/GPIO isn't ready (distinguishes a flat/unknown cell from
    // the old hardcoded 100 %). The per-sample printk in main.cpp shows the
    // raw mV so a wrong AIN mapping is obvious on the bench (expect ~3300-4200).
    const uint16_t mv = readBatteryMv();
    g_battery_mv = mv;
    out->battery_pct = batteryPctFromMv(mv);

    return any_ok;
}

uint16_t batteryMv() { return g_battery_mv; }

}  // namespace carl::room::sensors
