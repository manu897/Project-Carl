// Station-mode Wi-Fi for the Carl hub.
//
// V1 reads SSID + password from menuconfig (CARL_WIFI_SSID, CARL_WIFI_PASSWORD)
// and connects on boot. Phase 3f will replace this with a first-boot SoftAP
// captive portal that lets the user enter credentials from a phone — the
// public API stays the same, so the upgrade is contained to this module.

#pragma once

#include <stdbool.h>

void carl_wifi_init(void);

// Block until Wi-Fi is up or the timeout expires. Returns true on success.
bool carl_wifi_wait_connected(int timeout_ms);

// Read-only accessor for the bound IPv4 address (for mDNS / logs).
// Returns 0.0.0.0 if not connected.
unsigned int carl_wifi_local_ip(void);
