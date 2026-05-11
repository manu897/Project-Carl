/* Project-Carl — ESP32 hub.
 *
 * What's wired up as of Phase 3a:
 *   - Wi-Fi station-mode connect (creds from menuconfig — captive portal
 *     comes in Phase 3f).
 *   - mDNS advertising _carl-hub._tcp so iOS Bonjour discovery finds us
 *     and the M5Paper reader can resolve carl-hub.local.
 *   - HTTP server with /api/health (real data) and /api/nodes (empty array
 *     until Phase 3b lights up the BLE scanner + AES decrypt path).
 *
 * Live plan:
 *   /Users/fwdev/.claude/plans/work-on-carl-for-silly-lark.md
 */

#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_scanner.h"
#include "http_api.h"
#include "key_store.h"
#include "mdns_service.h"
#include "node_registry.h"
#include "wifi_sta.h"

static const char *TAG = "carl-hub";

void app_main(void) {
    ESP_LOGI(TAG, "Project-Carl hub booting");

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    carl_node_registry_init();
    carl_key_store_init();
    carl_wifi_init();

    if (!carl_wifi_wait_connected(20000)) {
        ESP_LOGE(TAG, "Wi-Fi not up after 20 s — running offline. The "
                      "captive-portal first-boot flow will replace this in "
                      "Phase 3f. Reconfigure SSID via menuconfig + reflash.");
        return;
    }

    carl_mdns_start();
    carl_http_api_start();

    // BLE scanner needs the BT controller, which conflicts with WiFi only on
    // older ESP32 chips during heavy radio use. We start it after Wi-Fi is
    // settled so co-existence has a known steady state.
    carl_ble_scanner_start();

    ESP_LOGI(TAG, "hub up — http://" CONFIG_CARL_MDNS_HOSTNAME ".local/");
}
