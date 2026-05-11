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

// Power-management primitives. Map to the SSD1306's blanking command:
// the panel switches to low-current sleep mode (~10 µA) when blanked.
// Entire framebuffer is preserved so wake() resumes whatever was last drawn.
void wake();
void sleep();

// Leaf splash — Carl brand mark + "Project Carl" text. Held on the screen
// while the boot chime plays. Same artwork direction as the iOS app icon.
void showLeafSplash();

// First-boot welcome screen, optionally showing the freshly-generated AES key
// hex so the user can transcribe it into the hub UI.
void showWelcome(const char* key_hex_or_null);

// Periodic readings page. `live` draws a small "L" indicator in the top-right
// corner so the user can see they're in the short-press-triggered live mode.
void showReadings(const carl::sensors::Sample& s, carl::thresholds::Severity sev,
                  bool live = false);

// Debug-mode readings page — packs raw ADC, calibration, and severity onto
// the OLED instead of friendly user-facing text. Only used in builds with
// CONFIG_CARL_DEBUG_MODE=y. `sample_no` is a rolling sample counter so you
// can see the loop ticking even when sensor values are stable.
void showDebugReadings(const carl::sensors::Sample& s,
                       carl::thresholds::Severity sev,
                       uint32_t sample_no);

// Two-line status used by the calibration flow.
void showCalibrationStep(const char* title, const char* hint);

// Render the provisioning QR (`carl://node?mac=…&key=…`) on the OLED for
// the iOS app's Add-Plant flow to scan. Returns false if QR rendering is
// disabled (CONFIG_CARL_HAS_QR=n) or QR generation failed.
//
//   mac6  : node's BLE MAC, little-endian (6 bytes), as returned by bt_id_get
//   key16 : 16-byte AES key
bool showProvisioningQR(const uint8_t mac6[6], const uint8_t key16[16]);

// Show an error code / message and freeze.
void showError(const char* msg);

}  // namespace carl::ui::oled

#endif  // CONFIG_CARL_DISPLAY_PROFILE
