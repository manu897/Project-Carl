// Sensor sampling for the Carl node — BME280 (T/H/P), VEML7700 (lux),
// soil-moisture probe (ADC). All three are wired in the devicetree overlay.
// Soil readings are converted to a 0..100% via the persisted dry/wet
// calibration; raw ADC is also exposed for the calibration UI.

#pragma once

#include <cstdint>

namespace carl::sensors {

struct Sample {
    // Each environmental field has its own _ok flag because different sensor
    // combinations populate different subsets — e.g. a BME280 fills all three,
    // a Grove Temperature Sensor v1.2 only fills temperature.
    bool     temp_ok;
    float    temperature_c;   // °C
    bool     humidity_ok;
    float    humidity_pct;    // %
    bool     pressure_ok;
    float    pressure_hpa;    // hPa

    bool     veml_ok;
    float    illuminance_lux; // lux

    bool     soil_ok;
    uint16_t soil_raw_adc;    // raw 12-bit ADC count
    float    soil_pct;        // 0..100% (clamped); requires calibration

    uint8_t  battery_pct;     // 0..100%
};

bool init();

// Sample everything; populates *out. Returns true if at least one channel
// succeeded. Individual *_ok flags indicate per-channel success.
bool sample(Sample* out);

// Read the soil probe's raw ADC value once. Used by the calibration flow.
bool readSoilRaw(uint16_t* raw);

// Apply or update the dry/wet calibration. Persists to NVS via settings.
void setSoilCalibration(uint16_t dry_raw, uint16_t wet_raw);

// Whether calibration has ever been performed.
bool hasSoilCalibration();

// Raw dry / wet calibration ADC values, for the debug-mode display.
uint16_t soilDryRaw();
uint16_t soilWetRaw();

// Last successful battery voltage in millivolts (0 = not yet read /
// last read failed). Updated each call to `sample()`.
uint16_t batteryMv();

}  // namespace carl::sensors
