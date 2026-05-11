// User button on the XIAO Expansion Board (D1, GPIO P0.18, active-low).
// Latched short-press / long-press events; main loop polls via consume*().

#pragma once

#ifdef CONFIG_CARL_DISPLAY_PROFILE

namespace carl::ui::button {

bool init();

// Returns true if a short press has been recorded since the last call,
// clearing the latch.
bool consumeShortPress();

// Returns true if a long press (>= 2s) has been recorded.
bool consumeLongPress();

// Synchronous: is the button currently held? Used for boot-time detection.
bool isHeldNow();

// Block up to timeout_ms or until any button edge (press or release) wakes
// us. Used in place of k_sleep() so the main loop becomes responsive to
// presses even while the next scheduled sample is far in the future
// (sample interval can be 30 min in customer mode). Returns 0 on event,
// non-zero on timeout — caller doesn't usually care which.
int waitForAnyEvent(int timeout_ms);

}  // namespace carl::ui::button

#endif  // CONFIG_CARL_DISPLAY_PROFILE
