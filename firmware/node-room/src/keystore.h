// AES-CCM key + monotonic counter for BTHome v2 broadcasts.
//
// The 16-byte key is generated from the hardware RNG on first boot and
// persisted to NVS via the settings subsystem. The hub gets the key once via
// out-of-band provisioning (OLED display on the display profile, or via the
// connectable calibration window on the cheap profile). The 4-byte counter
// increments per advertisement and is checkpointed to NVS periodically.

#pragma once

#include <cstdint>

namespace carl::keystore {

// Initialize the settings subsystem, load (or first-boot generate) the key
// and counter. Must be called once after settings_subsys_init() / settings_load().
// Returns true on success.
bool init();

// Pointer to the 16-byte key. Stable until reboot.
const uint8_t* key();

// Whether the key was just generated this boot — caller should display it.
bool isFreshKey();

// Render the key as a 32-char uppercase hex string into out (>= 33 bytes).
void keyHex(char out[33]);

// Get the next counter value to use for an advertisement. Bumps + may
// checkpoint to NVS.
uint32_t nextCounter();

}  // namespace carl::keystore
