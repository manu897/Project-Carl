// RGB LED control for the Thingy:53 room node.
//
// Uses the onboard PWM-driven RGB LED (pwm0 channels 0/1/2) for status
// feedback. All calls are non-blocking except flashGreen/flashRed which
// hold for the blink duration.
//
// Colour conventions (following standard embedded practice):
//   green  — success / healthy broadcast
//   red    — error / failure
//   blue   — idle / standby
//   rainbow — boot animation

#pragma once

#include <cstdint>

namespace carl::room::ui::led {

bool init();

// Set the LED to an arbitrary RGB colour (0-255 per channel).
// Non-blocking — the LED stays at this colour until changed.
void setRGB(uint8_t r, uint8_t g, uint8_t b);

// Turn all channels off.
void off();

// Brief green flash (blocking for `ms` milliseconds).
void flashGreen(int ms = 80);

// Brief red flash (blocking for `ms` milliseconds).
void flashRed(int ms = 150);

// `count` blinks of (r,g,b), each on for `on_ms` with `gap_ms` between,
// ending dark. Blocking. Used for status alerts (battery-low, poor air)
// layered onto the normal per-broadcast blink — blink count distinguishes
// which condition without needing more colours.
void blinkStatus(uint8_t r, uint8_t g, uint8_t b, int count,
                  int on_ms = 120, int gap_ms = 120);

}  // namespace carl::room::ui::led
