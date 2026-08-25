// Sensor sampling for the Carl room-monitor node (Thingy:53).
//
// Reads the onboard BME688 (T/H/P + gas resistance) and BH1749 (RGB + IR
// → photopic lux), plus the board's VBAT divider for battery state.
// Same Sample shape as firmware/node-sensor/src/sensors.h so the BTHome
// emitter can share code between probe + room variants.

#pragma once

#include <cstdint>

namespace carl::room::sensors {

// Heuristic air-quality bucket derived from BME688 gas resistance, relative
// to a self-learned "cleanest air seen" baseline. NOT a calibrated VOC/IAQ
// value — that needs Bosch's proprietary BSEC library, which this firmware
// doesn't link. Local-feedback only (RGB); deliberately not broadcast over
// BTHome, since mislabeling a heuristic as the wire format's VOC object
// (which expects real µg/m³) would misrepresent the data to Home Assistant.
enum class AirQuality : uint8_t { kUnknown, kGood, kModerate, kPoor };

struct Sample {
    bool     temp_ok;
    float    temperature_c;   // °C — BME688
    bool     humidity_ok;
    float    humidity_pct;    // % — BME688
    bool     pressure_ok;
    float    pressure_hpa;    // hPa — BME688

    bool     lux_ok;
    float    illuminance_lux; // lux — BH1749 green channel × photopic scale

    // BME688 gas resistance in ohms. Higher = cleaner air. Not currently
    // broadcast — wire format wants a proper VOC index in ppb (BTHome 0x13)
    // which BME688 only produces via Bosch's BSEC binary blob. Kept here
    // for future use / OLED debug.
    bool     gas_ok;
    uint32_t gas_resistance_ohm;
    AirQuality air_quality;   // heuristic bucket from gas_resistance_ohm; kUnknown if !gas_ok

    uint8_t  battery_pct;     // 0..100 % — VBAT divider on ADC ch 2
};

// Initialise the onboard sensors. Loud printk on success / failure so the
// USB CDC log makes it obvious when wiring or a driver isn't right.
// Returns true if at least one sensor initialised.
bool init();

// Sample every sensor that initialised. Per-channel *_ok flags indicate
// per-channel success; returns true if at least one channel succeeded.
bool sample(Sample* out);

// Last successful battery voltage in millivolts (0 = not yet read / failed).
uint16_t batteryMv();

}  // namespace carl::room::sensors
