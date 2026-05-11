#include "wifi_setup.h"

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>

namespace carl::wifi {

bool ensureConnected() {
#ifdef CARL_READER_USE_MOCK
    // Mock mode skips Wi-Fi entirely — useful for hardware-only testing
    // without a hub or router involved. Dashboard still renders mock data.
    Serial.println("[wifi] CARL_READER_USE_MOCK set — skipping Wi-Fi");
    return false;
#else
    WiFiManager wm;
    wm.setConfigPortalTimeout(180);  // 3 min captive-portal timeout
    wm.setConnectTimeout(20);

    // AP name shown in the user's Wi-Fi list when no creds are stored yet.
    if (!wm.autoConnect("Carl-Reader-Setup")) {
        Serial.println("[wifi] portal timed out — running offline");
        return false;
    }
    Serial.printf("[wifi] connected to %s, ip=%s\n",
                  WiFi.SSID().c_str(),
                  WiFi.localIP().toString().c_str());
    return true;
#endif
}

}  // namespace carl::wifi
