/* Project-Carl — ESP32 hub.
 *
 * Boot paths:
 *   - Credentials present + Wi-Fi joins → normal mode: SNTP, mDNS, REST API +
 *     web dashboard, BLE scanner.
 *   - No credentials / can't join → setup mode: SoftAP "Carl-Hub-Setup" +
 *     captive-portal DNS + the Wi-Fi setup page. POST /api/setup/wifi stores
 *     creds and reboots into normal mode (Phase 3f).
 */

#include "esp_log.h"
#include "nvs_flash.h"

#include "ble_scanner.h"
#include "captive_portal.h"
#include "http_api.h"
#include "key_store.h"
#include "mdns_service.h"
#include "mqtt_bridge.h"
#include "node_registry.h"
#include "norman_uplink.h"
#include "time_sync.h"
#include "wifi_sta.h"

static const char *TAG = "carl-hub";

#define SETUP_AP_SSID "Carl-Hub-Setup"

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

    // Try station mode if we have credentials; otherwise go straight to setup.
    const bool joined = carl_wifi_has_creds() && carl_wifi_connect_sta(20000);

    if (!joined) {
        ESP_LOGW(TAG, "no Wi-Fi — entering setup mode (SoftAP '%s')", SETUP_AP_SSID);
        carl_wifi_start_softap(SETUP_AP_SSID);
        carl_captive_portal_start();      // DNS hijack → 192.168.4.1
        carl_http_set_setup_mode(true);   // every page serves setup.html
        carl_http_api_start();            // hosts setup.html + POST /api/setup/wifi
        ESP_LOGI(TAG, "join '%s' and open http://192.168.4.1/ to configure Wi-Fi", SETUP_AP_SSID);
        return;  // stay in setup mode until the user submits creds (then reboot)
    }

    // --- Normal mode ---
    carl_time_sync_start();   // real ISO-8601 timestamps a few seconds later
    carl_mdns_start();
    carl_http_api_start();    // REST API + web dashboard

    // BLE scanner after Wi-Fi settles so radio co-existence has a steady state.
    carl_ble_scanner_start();

    // Optional outbound bridges — each is a no-op unless enabled in menuconfig.
    carl_mqtt_bridge_start();   // → Home Assistant (local MQTT discovery)
    carl_norman_uplink_start(); // → Project-Norman (continuous cloud MQTT)

    ESP_LOGI(TAG, "hub up — http://" CONFIG_CARL_MDNS_HOSTNAME ".local/");
}
