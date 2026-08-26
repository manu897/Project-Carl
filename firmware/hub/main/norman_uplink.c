#include "norman_uplink.h"

// ESP-IDF does NOT make CONFIG_* Kconfig macros visible automatically — every
// file that checks one must include this generated header explicitly.
// Without it, CONFIG_CARL_NORMAN_ENABLE is silently undefined and the #ifdef
// below always falls through to the empty stub, no matter what's set in
// menuconfig/sdkconfig. (Bug found 2026-08-25 — norman_uplink.c and
// mqtt_bridge.c both omitted this; key_store.c had it right from the start.)
#include "sdkconfig.h"

#ifdef CONFIG_CARL_NORMAN_ENABLE

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "node_registry.h"

#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include "esp_crt_bundle.h"
#endif

static const char *TAG = "carl-norman";
#define SITE CONFIG_CARL_NORMAN_SITE_ID

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;

static void publish_node(const carl_node_snapshot_t *n, void *user) {
    (void)user;
    const carl_reading_t *r = &n->latest;

    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "node", n->id);
    cJSON_AddStringToObject(o, "mac", n->mac);
    cJSON_AddStringToObject(o, "name", n->name);
    // Bug found 2026-08-26 (via the iOS + Norman sessions): this payload never
    // carried node classification, so Norman's MQTT ingest had nothing to key
    // off and hardcoded every node — including real room nodes — as "plant"
    // on first insert. carl_node_snapshot_t now carries node_type/room_id
    // (added for the hub's own REST API #16 room-join feature, but never
    // threaded through to the MQTT payload until now).
    cJSON_AddStringToObject(o, "node_type", n->node_type);
    if (n->room_id != NULL && n->room_id[0] != '\0') {
        cJSON_AddStringToObject(o, "room_id", n->room_id);
    }
    cJSON_AddBoolToObject(o, "online", n->online);
    if (r->soil_ok)     cJSON_AddNumberToObject(o, "soil_pct", r->soil_pct);
    if (r->temp_ok)     cJSON_AddNumberToObject(o, "temperature_c", r->temp_c);
    if (r->humidity_ok) cJSON_AddNumberToObject(o, "humidity_pct", r->humidity_pct);
    if (r->pressure_ok) cJSON_AddNumberToObject(o, "pressure_hpa", r->pressure_hpa);
    if (r->lux_ok)      cJSON_AddNumberToObject(o, "illuminance_lux", r->lux);
    if (r->battery_ok)  cJSON_AddNumberToObject(o, "battery_pct", r->battery_pct);

    char *body = cJSON_PrintUnformatted(o);
    char topic[64];
    snprintf(topic, sizeof(topic), "carl/%s/%s", SITE, n->id);
    // QoS 1 so cloud ingestion doesn't silently drop a sample on a flaky link.
    esp_mqtt_client_publish(s_client, topic, body, 0, /*qos*/1, /*retain*/0);
    free(body);
    cJSON_Delete(o);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    (void)handler_args; (void)base; (void)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:    s_connected = true;  ESP_LOGI(TAG, "connected to Norman"); break;
        case MQTT_EVENT_DISCONNECTED: s_connected = false; ESP_LOGW(TAG, "disconnected"); break;
        default: break;
    }
}

static void publish_task(void *arg) {
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_CARL_NORMAN_INTERVAL_SEC * 1000);
    while (1) {
        if (s_connected) carl_node_registry_foreach(publish_node, NULL);
        vTaskDelay(period);
    }
}

void carl_norman_uplink_start(void) {
    if (strlen(CONFIG_CARL_NORMAN_BROKER_URI) == 0) {
        ESP_LOGW(TAG, "no broker URI — Norman uplink disabled");
        return;
    }
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_CARL_NORMAN_BROKER_URI,
    };
    if (strlen(CONFIG_CARL_NORMAN_USERNAME) > 0)
        cfg.credentials.username = CONFIG_CARL_NORMAN_USERNAME;
    if (strlen(CONFIG_CARL_NORMAN_PASSWORD) > 0)
        cfg.credentials.authentication.password = CONFIG_CARL_NORMAN_PASSWORD;
#ifdef CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
    // Verify the cloud broker's TLS cert against the bundled root CAs when the
    // URI is mqtts://. Harmless for plain mqtt:// (ignored).
    cfg.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) { ESP_LOGE(TAG, "client init failed"); return; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    xTaskCreate(publish_task, "carl_norman", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "Norman uplink → %s as carl/%s/* (every %ds)",
             CONFIG_CARL_NORMAN_BROKER_URI, SITE, CONFIG_CARL_NORMAN_INTERVAL_SEC);
}

#else  // !CONFIG_CARL_NORMAN_ENABLE

void carl_norman_uplink_start(void) {}

#endif
