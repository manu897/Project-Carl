// Wi-Fi for the Carl hub: station mode in normal operation, SoftAP for
// first-boot onboarding.
//
// Credential precedence (Phase 3f):
//   1. NVS (set by the captive-portal /api/setup/wifi flow)
//   2. menuconfig CONFIG_CARL_WIFI_SSID/PASSWORD (bench fallback)
// If neither yields a connection, main() drops into SoftAP "setup mode" and
// brings up the captive portal so the user can enter their home Wi-Fi.

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Bring up the Wi-Fi stack (netif, driver, event handlers). Call once.
void carl_wifi_init(void);

// True if usable credentials exist (NVS, or a non-placeholder menuconfig SSID).
bool carl_wifi_has_creds(void);

// Load credentials, connect in station mode, and block until connected or the
// timeout expires. Returns true on success.
bool carl_wifi_connect_sta(int timeout_ms);

// Start SoftAP mode for the captive portal. `ap_ssid` is the open network the
// user joins (e.g. "Carl-Hub-Setup"). The hub is reachable at 192.168.4.1.
void carl_wifi_start_softap(const char *ap_ssid);

// Persist home Wi-Fi credentials to NVS (used by /api/setup/wifi). The caller
// typically reboots afterwards so the new creds take effect cleanly.
bool carl_wifi_save_creds(const char *ssid, const char *psk);

// Bound IPv4 address (STA mode), or 0 if not connected.
unsigned int carl_wifi_local_ip(void);

// RSSI of the associated AP in dBm. False if unavailable (e.g. SoftAP mode).
bool carl_wifi_rssi_dbm(int *out_dbm);

#ifdef __cplusplus
}
#endif
