// Piezo buzzer on the Thingy:53 (PWM1, P1.15).
//
// Same Nokia welcome chime as the plant-probe node, plus tone() for
// general-purpose beeps. The buzzer is only used at boot and on errors;
// steady-state operation is silent.

#pragma once

#include <cstdint>

namespace carl::room::ui::buzzer {

bool init();

// Play a single tone at the given frequency for `ms` milliseconds.
// Blocking. Pass hz=0 for a silent pause.
void tone(uint32_t hz, int ms);

// Silence the buzzer immediately.
void off();

// Play the Nokia welcome chime (~3.3 s). Blocking.
void welcomeChime();

// Short error beep pattern (3 fast descending tones, ~400 ms). Blocking.
void errorBeep();

}  // namespace carl::room::ui::buzzer
