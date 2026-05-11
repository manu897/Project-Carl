// First-boot Wi-Fi setup for the M5Paper reader. On boot:
//   - if NVS has stored credentials, connect silently.
//   - if not, raise a captive-portal AP named "Carl-Reader-Setup". The user
//     joins it from their phone, picks home Wi-Fi, enters password. Saved
//     to NVS and reconnect logic takes over from there.

#pragma once

namespace carl::wifi {

// Blocks until Wi-Fi is up (connected to a network or via captive portal).
// Returns false only if the captive-portal flow was abandoned past the
// timeout; in that case the reader continues in mock-mode style — the
// dashboard still renders but no live data flows.
bool ensureConnected();

}  // namespace carl::wifi
