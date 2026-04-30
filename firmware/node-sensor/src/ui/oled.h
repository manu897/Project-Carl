// SSD1306 OLED rendering for the display profile. Text-only via Zephyr's
// character framebuffer (CONFIG_CHARACTER_FRAMEBUFFER) — no LVGL overhead.
//
// All entry points are no-ops on the cheap profile (header guarded so the
// linker never sees the calls).

#pragma once

#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "sensors.h"
#include "thresholds.h"

namespace carl::ui::oled {

bool init();

// First-boot welcome screen, optionally showing the freshly-generated AES key
// hex so the user can transcribe it into the hub UI.
void showWelcome(const char* key_hex_or_null);

// Periodic readings page.
void showReadings(const carl::sensors::Sample& s, carl::thresholds::Severity sev);

// Two-line status used by the calibration flow.
void showCalibrationStep(const char* title, const char* hint);

// Show an error code / message and freeze.
void showError(const char* msg);

}  // namespace carl::ui::oled

#endif  // CONFIG_CARL_DISPLAY_PROFILE
