// Soil-moisture thresholds + "thirsty" severity classification.
//
// Severity is computed locally from the current sample on every cycle. The
// resulting level rides along in the BTHome advertisement (Carl-namespace
// payload — to be added when we extend the wire format) and also drives the
// local buzzer + OLED feedback on the display profile.

#pragma once

#include <cstdint>

namespace carl::thresholds {

enum class Severity : uint8_t {
    kOk       = 0,
    kWarning  = 1,
    kCritical = 2,
};

// Persisted thresholds (default: warn at <30%, critical at <15%).
struct Config {
    uint8_t warn_pct;
    uint8_t crit_pct;
};

bool init();

Severity evaluate(float soil_pct);

const Config& config();
void setConfig(Config c);

}  // namespace carl::thresholds
