// RGB status LED for the Carl node — the XIAO nRF52840's onboard RGB
// (led0/led1/led2 = red/green/blue, active-low GPIO).
//
// The screenless budget node uses this as its status surface. To protect the
// battery the LED is NEVER left solid-on — it follows the same power-aware
// pattern as the OLED: dark by default, a brief blink once per sample to
// convey state, then off. Average current is negligible (a ~100 ms pulse per
// 30-min sample ≈ 0.005 % duty). Severity → colour:
//   OK       → one short green blink (a quiet "alive + healthy" heartbeat)
//   Warning  → two amber blinks
//   Critical → three red blinks
//
// Entirely compiled out unless CONFIG_CARL_HAS_RGB — the calls become no-ops.

#pragma once

#include "thresholds.h"

namespace carl::ui::rgb {

// Configure the three LED GPIOs. Returns false if any pin isn't ready.
bool init();

// All channels off.
void off();

// Blink the severity colour briefly, then leave the LED OFF. Blocking for the
// blink duration (longer for more severe states). Call once per sample — this
// is the screenless node's status notification, kept momentary to save power.
void blinkSeverity(carl::thresholds::Severity s);

// Brief power-on colour sweep (~600 ms, blocking) so the user sees the
// node booted — the RGB analogue of the buzzer's welcome chime.
void bootFlash();

}  // namespace carl::ui::rgb
