#include "sensors.h"

#include <cstring>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

namespace carl::sensors {

namespace {

const struct device* g_bme  = DEVICE_DT_GET_ANY(bosch_bme280);
const struct device* g_veml = DEVICE_DT_GET_ANY(vishay_veml7700);

const struct adc_dt_spec g_soil_adc =
    ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

uint16_t g_dry_raw = 0;
uint16_t g_wet_raw = 0;
bool     g_calibrated = false;

constexpr const char* kCalDryPath = "carl/cal/dry";
constexpr const char* kCalWetPath = "carl/cal/wet";

int settingsCb(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg) {
    const char* next = nullptr;
    int name_len = settings_name_next(name, &next);
    // We expect "cal/dry" or "cal/wet" — settings_name_next gives us the
    // first component, then we descend.
    if (name_len == 3 && std::strncmp(name, "cal", 3) == 0 && next != nullptr) {
        if (std::strcmp(next, "dry") == 0 && len == sizeof(g_dry_raw)) {
            read_cb(cb_arg, &g_dry_raw, sizeof(g_dry_raw));
            g_calibrated = (g_wet_raw != 0);
        } else if (std::strcmp(next, "wet") == 0 && len == sizeof(g_wet_raw)) {
            read_cb(cb_arg, &g_wet_raw, sizeof(g_wet_raw));
            g_calibrated = (g_dry_raw != 0);
        }
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(carl_cal, "carl",
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
    if (g_bme && !device_is_ready(g_bme))   g_bme  = nullptr;
    if (g_veml && !device_is_ready(g_veml)) g_veml = nullptr;
    if (!device_is_ready(g_soil_adc.dev))   return false;
    return adc_channel_setup_dt(&g_soil_adc) == 0;
}

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

void setSoilCalibration(uint16_t dry_raw, uint16_t wet_raw) {
    g_dry_raw = dry_raw;
    g_wet_raw = wet_raw;
    g_calibrated = (dry_raw != 0 && wet_raw != 0 && dry_raw != wet_raw);
    settings_save_one(kCalDryPath, &g_dry_raw, sizeof(g_dry_raw));
    settings_save_one(kCalWetPath, &g_wet_raw, sizeof(g_wet_raw));
}

bool hasSoilCalibration() { return g_calibrated; }

bool sample(Sample* out) {
    *out = Sample{};
    bool any_ok = false;

    if (g_bme && sensor_sample_fetch(g_bme) == 0) {
        struct sensor_value v{};
        if (sensor_channel_get(g_bme, SENSOR_CHAN_AMBIENT_TEMP, &v) == 0) {
            out->temperature_c = sensor_value_to_double(&v);
            out->bme_ok = true; any_ok = true;
        }
        if (sensor_channel_get(g_bme, SENSOR_CHAN_HUMIDITY, &v) == 0) {
            out->humidity_pct = sensor_value_to_double(&v);
        }
        if (sensor_channel_get(g_bme, SENSOR_CHAN_PRESS, &v) == 0) {
            // Zephyr returns kPa; BTHome wants hPa.
            out->pressure_hpa = sensor_value_to_double(&v) * 10.0f;
        }
    }

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

    // TODO(phase 4): real battery monitoring via VDDH or fuel-gauge IC.
    out->battery_pct = 100;

    return any_ok;
}

}  // namespace carl::sensors
