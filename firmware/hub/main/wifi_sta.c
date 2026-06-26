#include "wifi_sta.h"

#include <string.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs.h"
#include "sdkconfig.h"

static const char *TAG = "carl-wifi";

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1
#define MAX_RETRY 8

#define NVS_WIFI_NAMESPACE "carl-wifi"
// menuconfig default placeholder — treat as "no credentials" so a fresh hub
// drops into the captive portal instead of failing to join a fake SSID.
#define SSID_PLACEHOLDER "carl-hub-needs-config"

static EventGroupHandle_t s_event_group = NULL;
static esp_netif_t       *s_sta_netif   = NULL;
static esp_netif_t       *s_ap_netif    = NULL;
static int                s_retry_count = 0;
static unsigned int       s_local_ip    = 0;

static void event_handler(void *arg, esp_event_base_t base, int32_t id, void *data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "retry connect (%d/%d)", s_retry_count, MAX_RETRY);
        } else {
            ESP_LOGW(TAG, "connect failed after %d retries", MAX_RETRY);
            if (s_event_group) xEventGroupSetBits(s_event_group, WIFI_FAIL_BIT);
        }
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *ev = (ip_event_got_ip_t *)data;
        s_local_ip = ev->ip_info.ip.addr;
        ESP_LOGI(TAG, "got ip " IPSTR, IP2STR(&ev->ip_info.ip));
        s_retry_count = 0;
        if (s_event_group) xEventGroupSetBits(s_event_group, WIFI_CONNECTED_BIT);
    }
}

void carl_wifi_init(void) {
    s_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif  = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t any_id_handle, got_ip_handle;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &any_id_handle));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, &got_ip_handle));
}

// Load the SSID/PSK to use: NVS first, then menuconfig fallback. Writes
// NUL-terminated strings into the caller's buffers. Returns true if a usable
// (non-placeholder, non-empty) SSID was found.
static bool load_creds(char ssid[33], char psk[64]) {
    ssid[0] = '\0';
    psk[0] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t sl = 33, pl = 64;
        nvs_get_str(h, "ssid", ssid, &sl);
        nvs_get_str(h, "psk", psk, &pl);
        nvs_close(h);
    }
    if (ssid[0] != '\0') return true;

    // Fall back to menuconfig.
    strncpy(ssid, CONFIG_CARL_WIFI_SSID, 32);     ssid[32] = '\0';
    strncpy(psk,  CONFIG_CARL_WIFI_PASSWORD, 63); psk[63]  = '\0';
    if (ssid[0] == '\0' || strcmp(ssid, SSID_PLACEHOLDER) == 0) {
        ssid[0] = '\0';
        return false;
    }
    return true;
}

bool carl_wifi_has_creds(void) {
    char ssid[33], psk[64];
    return load_creds(ssid, psk);
}

bool carl_wifi_connect_sta(int timeout_ms) {
    char ssid[33], psk[64];
    if (!load_creds(ssid, psk)) {
        ESP_LOGW(TAG, "no Wi-Fi credentials — staying out of STA mode");
        return false;
    }

    wifi_config_t wifi_config = { 0 };
    strncpy((char *)wifi_config.sta.ssid,     ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, psk,  sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = (psk[0] == '\0') ? WIFI_AUTH_OPEN : WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable    = true;

    s_retry_count = 0;
    xEventGroupClearBits(s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "joining %s…", ssid);

    EventBits_t bits = xEventGroupWaitBits(
        s_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
        pdFALSE, pdFALSE, pdMS_TO_TICKS(timeout_ms));
    return (bits & WIFI_CONNECTED_BIT) != 0;
}

void carl_wifi_start_softap(const char *ap_ssid) {
    wifi_config_t ap = { 0 };
    strncpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen(ap_ssid);
    ap.ap.channel = 1;
    ap.ap.max_connection = 4;
    ap.ap.authmode = WIFI_AUTH_OPEN;  // open network — captive portal, no secret

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "SoftAP '%s' up — connect and browse to http://192.168.4.1/", ap_ssid);
}

bool carl_wifi_save_creds(const char *ssid, const char *psk) {
    if (ssid == NULL || ssid[0] == '\0') return false;
    nvs_handle_t h;
    if (nvs_open(NVS_WIFI_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = nvs_set_str(h, "ssid", ssid);
    if (e == ESP_OK) e = nvs_set_str(h, "psk", psk ? psk : "");
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    if (e == ESP_OK) ESP_LOGI(TAG, "saved Wi-Fi creds for %s", ssid);
    return e == ESP_OK;
}

unsigned int carl_wifi_local_ip(void) {
    return s_local_ip;
}

bool carl_wifi_rssi_dbm(int *out_dbm) {
    if (out_dbm == NULL) return false;
    wifi_ap_record_t ap;
    if (esp_wifi_sta_get_ap_info(&ap) != ESP_OK) return false;
    *out_dbm = ap.rssi;
    return true;
}
