#include "sensors.h"

#include <cmath>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

namespace carl::sensors {

namespace {

const struct device* g_bme  = DEVICE_DT_GET_ANY(bosch_bme280);
const struct device* g_veml = DEVICE_DT_GET_ANY(vishay_veml7700);

const struct adc_dt_spec g_soil_adc =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 0);

const struct adc_dt_spec g_temp_adc =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 1);

// XIAO nRF52840 on-board VBAT divider: AIN7 (P0.31) reads VBAT × 0.3377,
// gated by P0.14 (active-low). Both come from the zephyr,user node in
// the board overlay so we can swap board variants without code edits.
const struct adc_dt_spec g_vbat_adc =
    ADC_DT_SPEC_GET_BY_IDX(DT_PATH(zephyr_user), 2);
const struct gpio_dt_spec g_vbat_enable =
    GPIO_DT_SPEC_GET(DT_PATH(zephyr_user), vbat_enable_gpios);

// Last successful battery read in millivolts — kept around for the OLED's
// debug view and for `printk` diagnostics. 0 = never read or read failed.
uint16_t g_battery_mv = 0;

uint16_t g_dry_raw = 0;
uint16_t g_wet_raw = 0;
bool     g_calibrated = false;

// Own subtree (see comment in keystore.cpp): each settings handler needs
// a unique root so Zephyr's parse-and-lookup routes entries correctly.
constexpr const char* kCalDryPath = "carl_cal/dry";
constexpr const char* kCalWetPath = "carl_cal/wet";

int settingsCb(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg) {
    // Tree is "carl_cal" so `name` is "dry" or "wet".
    if (std::strcmp(name, "dry") == 0 && len == sizeof(g_dry_raw)) {
        const ssize_t got = read_cb(cb_arg, &g_dry_raw, sizeof(g_dry_raw));
        g_calibrated = (g_wet_raw != 0);
        printk("cal load dry=%u (read %d bytes)\n", g_dry_raw, (int)got);
    } else if (std::strcmp(name, "wet") == 0 && len == sizeof(g_wet_raw)) {
        const ssize_t got = read_cb(cb_arg, &g_wet_raw, sizeof(g_wet_raw));
        g_calibrated = (g_dry_raw != 0);
        printk("cal load wet=%u (read %d bytes)\n", g_wet_raw, (int)got);
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(carl_cal, "carl_cal",
                               nullptr, settingsCb, nullptr, nullptr);

float clamp01(float v) {
    if (v < 0.0f) return 0.0f;
    if (v > 1.0f) return 1.0f;
    return v;
}

float soilPctFromRaw(uint16_t raw) {
    if (!g_calibrated || g_dry_raw == g_wet_raw) return 0.0f;
    // Capacitive probe: higher raw = drier. percent = (dry - raw) / (dry - wet)
    const float numer = static_cast<float>(g_dry_raw) - static_cast<float>(raw);
    const float denom = static_cast<float>(g_dry_raw) - static_cast<float>(g_wet_raw);
    return 100.0f * clamp01(numer / denom);
}

}  // namespace

bool init() {
    // Be loud about what's missing on first boot so the user can wire it up
    // instead of guessing why "bat=y T=- H=- P=- soil=- lux=-" shows up at
    // the hub. These are I2C peripherals on the Grove bus — easy to forget
    // to plug a BME280 or VEML7700 in.
    if (g_bme == nullptr) {
        printk("sensors: BME280 not present in devicetree\n");
    } else if (!device_is_ready(g_bme)) {
        printk("sensors: BME280 device not ready (I2C wiring? addr 0x76?)\n");
        g_bme = nullptr;
    } else {
        printk("sensors: BME280 ready\n");
    }
    if (g_veml == nullptr) {
        printk("sensors: VEML7700 not present in devicetree\n");
    } else if (!device_is_ready(g_veml)) {
        printk("sensors: VEML7700 device not ready (I2C wiring? addr 0x10?)\n");
        g_veml = nullptr;
    } else {
        printk("sensors: VEML7700 ready\n");
    }

    if (!device_is_ready(g_soil_adc.dev)) {
        printk("sensors: soil ADC device not ready — aborting init\n");
        return false;
    }
    if (adc_channel_setup_dt(&g_soil_adc) != 0) {
        printk("sensors: soil ADC channel setup failed\n");
        return false;
    }
    printk("sensors: soil ADC ready on AIN0\n");

    // Factory calibration: the settings handler has already run (settings_load
    // in boot() precedes sensors::init()), so a stored user/dev cal would have
    // set g_calibrated. If nothing was stored, seed the built-in endpoints so
    // the buttonless cheap/budget node still reports a usable soil %. We do
    // NOT persist these — a stored cal always loads first and wins.
#ifdef CONFIG_CARL_SOIL_FACTORY_CAL
    if (!g_calibrated) {
        g_dry_raw = CONFIG_CARL_SOIL_FACTORY_DRY_ADC;
        g_wet_raw = CONFIG_CARL_SOIL_FACTORY_WET_ADC;
        g_calibrated = (g_dry_raw != g_wet_raw);
        printk("sensors: applied factory soil cal dry=%u wet=%u\n",
               g_dry_raw, g_wet_raw);
    }
#endif

    // Best-effort: the Grove temp thermistor is optional. If channel setup
    // fails, the readGroveTemp() path will simply error out at sample time.
    (void)adc_channel_setup_dt(&g_temp_adc);
    return true;
}

bool readGroveTempC(float* out) {
    int16_t buf = 0;
    struct adc_sequence seq{};
    seq.buffer      = &buf;
    seq.buffer_size = sizeof(buf);
    if (adc_sequence_init_dt(&g_temp_adc, &seq) != 0) return false;
    if (adc_read(g_temp_adc.dev, &seq) != 0) return false;
    if (buf <= 0 || buf >= 4095) return false;  // open / shorted

    // Grove Temperature Sensor v1.2: NCP18WF104F03RB NTC thermistor
    // (R0 = 100 kΩ at 25 °C, β = 4275), wired as one half of a voltage
    // divider with a 100 kΩ pull-up. The ratio raw/full only depends on
    // the divider, not Vcc — so this works at 3.3 V as well as 5 V.
    constexpr float kR0    = 100000.0f;
    constexpr float kBeta  = 4275.0f;
    constexpr float kT0Inv = 1.0f / 298.15f;  // 25 °C in kelvin
    const float r = kR0 * (4095.0f / static_cast<float>(buf) - 1.0f);
    const float inv_t = std::log(r / kR0) / kBeta + kT0Inv;
    *out = 1.0f / inv_t - 273.15f;
    return true;
}

// Read VBAT in millivolts. Briefly enables the divider, samples AIN7,
// then disables the divider so it doesn't drain ~2 µA continuously.
// Returns 0 on failure (caller treats that as "unknown" and uses 0%).
uint16_t readBatteryMv() {
    if (!gpio_is_ready_dt(&g_vbat_enable)) return 0;
    if (!device_is_ready(g_vbat_adc.dev))  return 0;

    // GPIO_OUTPUT_ACTIVE drives the pin to its devicetree-defined "active"
    // level; we configured the GPIO as GPIO_ACTIVE_LOW so ACTIVE = LOW =
    // divider enabled.
    if (gpio_pin_configure_dt(&g_vbat_enable, GPIO_OUTPUT_ACTIVE) != 0) {
        return 0;
    }
    // Settle: the divider has ~510 kΩ source impedance and the AIN7 trace
    // has a small parasitic cap; 2 ms is comfortably more than enough.
    k_msleep(2);

    int16_t buf = 0;
    struct adc_sequence seq{};
    seq.buffer      = &buf;
    seq.buffer_size = sizeof(buf);

    bool ok = false;
    if (adc_sequence_init_dt(&g_vbat_adc, &seq) == 0
        && adc_read(g_vbat_adc.dev, &seq) == 0
        && buf > 0) {
        // ADC: gain 1/6, reference 0.6 V internal → full-scale 3.6 V across
        // 4095 counts. Convert ADC counts → AIN7 mV → VBAT mV (undo divider).
        //   v_ain7 = buf × 3600 / 4095     (mV at the SAADC input)
        //   vbat   = v_ain7 / 0.3377       (1M / (1M + 510k) divider ratio)
        // Combine to one fixed-point step to avoid floats in the sample path:
        //   vbat_mv = buf × 3600 / 4095 × 1000 / 338
        //           ≈ buf × 26.0
        const int32_t v_ain7_mv = (static_cast<int32_t>(buf) * 3600) / 4095;
        const int32_t vbat_mv   = (v_ain7_mv * 1000) / 338;
        if (vbat_mv >= 2500 && vbat_mv <= 4500) {
            g_battery_mv = static_cast<uint16_t>(vbat_mv);
            ok = true;
        }
    }

    // Disable the divider regardless of read outcome to spare the leakage.
    (void)gpio_pin_configure_dt(&g_vbat_enable, GPIO_OUTPUT_INACTIVE);
    return ok ? g_battery_mv : 0;
}

// LiPo discharge curve is approximately linear between 3.30 V (empty) and
// 4.20 V (full) for the middle 80 % of capacity. Good enough for an
// indicator until we add a proper fuel gauge.
uint8_t batteryPctFromMv(uint16_t mv) {
    if (mv == 0)        return 0;
    if (mv >= 4200)     return 100;
    if (mv <= 3300)     return 0;
    return static_cast<uint8_t>((mv - 3300) * 100u / 900u);
}

uint16_t batteryMv() { return g_battery_mv; }

bool readSoilRaw(uint16_t* raw) {
    int16_t buf = 0;
    struct adc_sequence seq{};
    seq.buffer      = &buf;
    seq.buffer_size = sizeof(buf);
    if (adc_sequence_init_dt(&g_soil_adc, &seq) != 0) return false;
    if (adc_read(g_soil_adc.dev, &seq) != 0) return false;
    *raw = static_cast<uint16_t>(buf < 0 ? 0 : buf);
    return true;
}

bool readSoilAveraged(uint16_t* out, int samples) {
    uint32_t acc = 0;
    int got = 0;
    for (int i = 0; i < samples; ++i) {
        uint16_t r = 0;
        if (readSoilRaw(&r)) { acc += r; ++got; }
        k_msleep(20);
    }
    if (got == 0) return false;
    *out = static_cast<uint16_t>(acc / got);
    return true;
}

void setSoilCalibration(uint16_t dry_raw, uint16_t wet_raw) {
    g_dry_raw = dry_raw;
    g_wet_raw = wet_raw;
    g_calibrated = (dry_raw != 0 && wet_raw != 0 && dry_raw != wet_raw);
    const int rc1 = settings_save_one(kCalDryPath, &g_dry_raw, sizeof(g_dry_raw));
    const int rc2 = settings_save_one(kCalWetPath, &g_wet_raw, sizeof(g_wet_raw));
    printk("cal save dry=%u rc=%d  wet=%u rc=%d  calibrated=%d\n",
           g_dry_raw, rc1, g_wet_raw, rc2, g_calibrated);
}

bool hasSoilCalibration() { return g_calibrated; }

uint16_t soilDryRaw() { return g_dry_raw; }
uint16_t soilWetRaw() { return g_wet_raw; }

bool sample(Sample* out) {
    *out = Sample{};
    bool any_ok = false;

    if (g_bme && sensor_sample_fetch(g_bme) == 0) {
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
            out->pressure_hpa = sensor_value_to_double(&v) * 10.0f;
            out->pressure_ok = true;
        }
    }

#ifdef CONFIG_CARL_HAS_GROVE_TEMP
    // Fallback / parallel path: the Grove Temperature Sensor v1.2 thermistor
    // on AIN4. Used when no BME280 is present — fills only temperature.
    // Opt-in so a floating AIN4 doesn't produce phantom readings.
    if (!out->temp_ok) {
        float t = 0.0f;
        if (readGroveTempC(&t) && t > -40.0f && t < 85.0f) {
            out->temperature_c = t;
            out->temp_ok = true; any_ok = true;
        }
    }
#endif

    if (g_veml && sensor_sample_fetch(g_veml) == 0) {
        struct sensor_value v{};
        if (sensor_channel_get(g_veml, SENSOR_CHAN_LIGHT, &v) == 0) {
            out->illuminance_lux = sensor_value_to_double(&v);
            out->veml_ok = true; any_ok = true;
        }
    }

    uint16_t raw = 0;
    if (readSoilRaw(&raw)) {
        out->soil_ok      = true;
        out->soil_raw_adc = raw;
        out->soil_pct     = soilPctFromRaw(raw);
        any_ok = true;
    }

    // Real battery monitoring via the XIAO BLE's onboard VBAT divider on
    // AIN7. Falls back to 0 % when the GPIO/ADC isn't ready — distinguishes
    // a flat battery from an uninitialised radio in the hub log.
    const uint16_t mv = readBatteryMv();
    out->battery_pct = batteryPctFromMv(mv);

    return any_ok;
}

}  // namespace carl::sensors
