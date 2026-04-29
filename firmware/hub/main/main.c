/* Project-Carl — ESP32 hub.
 *
 * Skeleton only; real implementation lands in Phase 3 of the plan
 * (live at /Users/fwdev/.claude/plans/work-on-carl-for-silly-lark.md).
 *
 * Responsibilities (when fleshed out):
 *   - First-boot Wi-Fi captive portal; persist credentials to NVS.
 *   - Continuous BLE scan; decrypt BTHome v2 with per-node AES keys; update
 *     in-memory node registry + 24 h ring buffer (PSRAM).
 *   - HTTP server on port 80, mDNS as carl-hub.local — JSON API + web SPA.
 *   - Optional MQTT bridge to Home Assistant (HA discovery topics).
 *   - Optional daily HTTPS POST of a JSON digest to a Project-Norman endpoint.
 *   - Pull daily JPEG from the camera node via BLE central GATT.
 */

#include <stdio.h>

#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "carl-hub";

void app_main(void) {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_LOGI(TAG, "carl-hub boot — skeleton (no behavior yet)");
}
