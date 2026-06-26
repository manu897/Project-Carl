// RGB status LED for the Carl node — the XIAO nRF52840's onboard RGB
// (led0/led1/led2 = red/green/blue, active-low GPIO).
//
// The screenless budget node uses this as its status surface: a glance tells
// you the plant's state. Severity → colour follows the buzzer's convention:
//   OK       → green
//   Warning  → amber (red + green)
//   Critical → red
//
// Entirely compiled out unless CONFIG_CARL_HAS_RGB — the calls become no-ops.

#pragma once

#include "thresholds.h"

namespace carl::ui::rgb {

// Configure the three LED GPIOs. Returns false if any pin isn't ready.
bool init();

// All channels off.
void off();

// Map a severity to a colour and latch it until the next call.
void setSeverity(carl::thresholds::Severity s);

// Brief power-on colour sweep (~600 ms, blocking) so the user sees the
// node booted — the RGB analogue of the buzzer's welcome chime.
void bootFlash();

}  // namespace carl::ui::rgb
