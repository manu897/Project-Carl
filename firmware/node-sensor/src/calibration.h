// Soil dry/wet calibration flow, used in two places:
//
//   - Display profile: when the user holds the button at boot, OLED prompts
//     guide them through "place probe in dry soil" → press → "place in wet"
//     → press, then save.
//
//   - Cheap profile: a one-minute connectable window after first boot lets a
//     phone/hub provision dry+wet values over a vendor GATT characteristic.
//     (Stub for now — implementation in Phase 4.)

#pragma once

namespace carl::calibration {

#ifdef CONFIG_CARL_DISPLAY_PROFILE
// Drives the OLED + button calibration flow. Blocks until the user completes
// or the watchdog timeout (60 s) expires.
void runDisplayFlow();
#endif

}  // namespace carl::calibration
