// PWM buzzer on the XIAO Expansion Board (A3). Cadence depends on severity:
//   warning  : 1 short beep
//   critical : 3 fast beeps
// Driven from the main loop; the buzzer is silent on a fresh OK reading.

#pragma once

#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "thresholds.h"

namespace carl::ui::buzzer {

bool init();

// Sound the cadence appropriate to `s`. Blocking (~< 1 s for critical).
// No-op on Severity::kOk.
void alert(carl::thresholds::Severity s);

}  // namespace carl::ui::buzzer

#endif  // CONFIG_CARL_DISPLAY_PROFILE
