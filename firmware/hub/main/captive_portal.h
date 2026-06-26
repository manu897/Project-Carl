// Captive-portal DNS hijack for first-boot Wi-Fi onboarding.
//
// While the hub is in SoftAP "setup mode", this answers every DNS A query
// with the hub's AP address (192.168.4.1) so phones/laptops detect a captive
// portal and pop the setup page automatically. Paired with the HTTP server
// serving setup.html and POST /api/setup/wifi.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the DNS responder task on UDP port 53. Call only in SoftAP mode.
void carl_captive_portal_start(void);

#ifdef __cplusplus
}
#endif
