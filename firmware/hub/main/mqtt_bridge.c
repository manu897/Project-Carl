#include "mqtt_bridge.h"

// ESP-IDF does NOT make CONFIG_* Kconfig macros visible automatically — every
// file that checks one must include this generated header explicitly.
// Without it, CONFIG_CARL_MQTT_HA_ENABLE is silently undefined here, so the
// #ifdef always fell through to the empty stub regardless of menuconfig.
#include "sdkconfig.h"

#ifdef CONFIG_CARL_MQTT_HA_ENABLE

#include <stdio.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"

#include "node_registry.h"

static const char *TAG = "carl-mqtt-ha";
#define PREFIX CONFIG_CARL_MQTT_HA_DISCOVERY_PREFIX

static esp_mqtt_client_handle_t s_client = NULL;
static volatile bool s_connected = false;

// Publish one HA MQTT Discovery config (retained) for a node metric.
static void publish_discovery(const carl_node_snapshot_t *n, const char *metric,
                              const char *nice, const char *unit,
                              const char *dev_cla) {
    char uniq[48];   snprintf(uniq, sizeof(uniq), "carl_%s_%s", n->id, metric);
    char topic[96];  snprintf(topic, sizeof(topic), PREFIX "/sensor/%s/config", uniq);
    char stat[48];   snprintf(stat, sizeof(stat), "carl/%s/state", n->id);
    char tpl[48];    snprintf(tpl, sizeof(tpl), "{{ value_json.%s }}", metric);
    char dname[64];  snprintf(dname, sizeof(dname), "%s %s", n->name, nice);

    cJSON *c = cJSON_CreateObject();
    cJSON_AddStringToObject(c, "name", dname);
    cJSON_AddStringToObject(c, "uniq_id", uniq);
    cJSON_AddStringToObject(c, "stat_t", stat);
    cJSON_AddStringToObject(c, "val_tpl", tpl);
    cJSON_AddStringToObject(c, "stat_cla", "measurement");
    if (unit && unit[0])      cJSON_AddStringToObject(c, "unit_of_meas", unit);
    if (dev_cla && dev_cla[0]) cJSON_AddStringToObject(c, "dev_cla", dev_cla);

    cJSON *dev = cJSON_AddObjectToObject(c, "dev");
    cJSON *ids = cJSON_AddArrayToObject(dev, "ids");
    char devid[24]; snprintf(devid, sizeof(devid), "carl_%s", n->id);
    cJSON_AddItemToArray(ids, cJSON_CreateString(devid));
    cJSON_AddStringToObject(dev, "name", n->name);
    cJSON_AddStringToObject(dev, "mf", "Project Carl");
    cJSON_AddStringToObject(dev, "mdl", "plant probe");

    char *body = cJSON_PrintUnformatted(c);
    esp_mqtt_client_publish(s_client, topic, body, 0, /*qos*/0, /*retain*/1);
    free(body);
    cJSON_Delete(c);
}

static void publish_node(const carl_node_snapshot_t *n, void *user) {
    (void)user;
    const carl_reading_t *r = &n->latest;

    // State payload — only fields the node actually reports.
    cJSON *st = cJSON_CreateObject();
    if (r->soil_ok)     cJSON_AddNumberToObject(st, "soil", r->soil_pct);
    if (r->temp_ok)     cJSON_AddNumberToObject(st, "temp", r->temp_c);
    if (r->humidity_ok) cJSON_AddNumberToObject(st, "humidity", r->humidity_pct);
    if (r->pressure_ok) cJSON_AddNumberToObject(st, "pressure", r->pressure_hpa);
    if (r->lux_ok)      cJSON_AddNumberToObject(st, "lux", r->lux);
    if (r->battery_ok)  cJSON_AddNumberToObject(st, "battery", r->battery_pct);
    char *state = cJSON_PrintUnformatted(st);
    char state_topic[48]; snprintf(state_topic, sizeof(state_topic), "carl/%s/state", n->id);
    esp_mqtt_client_publish(s_client, state_topic, state, 0, 0, /*retain*/1);
    free(state);
    cJSON_Delete(st);

    // Discovery config (retained) for each present metric. HA dedupes on the
    // retained config topic, so re-publishing each cycle is harmless and
    // means newly-appearing metrics get registered automatically.
    if (r->soil_ok)     publish_discovery(n, "soil", "Soil", "%", NULL);
    if (r->temp_ok)     publish_discovery(n, "temp", "Temperature", "°C", "temperature");
    if (r->humidity_ok) publish_discovery(n, "humidity", "Humidity", "%", "humidity");
    if (r->pressure_ok) publish_discovery(n, "pressure", "Pressure", "hPa", "pressure");
    if (r->lux_ok)      publish_discovery(n, "lux", "Illuminance", "lx", "illuminance");
    if (r->battery_ok)  publish_discovery(n, "battery", "Battery", "%", "battery");
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
    (void)handler_args; (void)base; (void)event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_CONNECTED:
            s_connected = true;
            ESP_LOGI(TAG, "connected to broker");
            break;
        case MQTT_EVENT_DISCONNECTED:
            s_connected = false;
            ESP_LOGW(TAG, "disconnected");
            break;
        default:
            break;
    }
}

static void publish_task(void *arg) {
    (void)arg;
    const TickType_t period = pdMS_TO_TICKS(CONFIG_CARL_MQTT_HA_INTERVAL_SEC * 1000);
    while (1) {
        if (s_connected) carl_node_registry_foreach(publish_node, NULL);
        vTaskDelay(period);
    }
}

void carl_mqtt_bridge_start(void) {
    if (strlen(CONFIG_CARL_MQTT_HA_BROKER_URI) == 0) {
        ESP_LOGW(TAG, "no broker URI — HA bridge disabled");
        return;
    }
    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = CONFIG_CARL_MQTT_HA_BROKER_URI,
    };
    if (strlen(CONFIG_CARL_MQTT_HA_USERNAME) > 0)
        cfg.credentials.username = CONFIG_CARL_MQTT_HA_USERNAME;
    if (strlen(CONFIG_CARL_MQTT_HA_PASSWORD) > 0)
        cfg.credentials.authentication.password = CONFIG_CARL_MQTT_HA_PASSWORD;

    s_client = esp_mqtt_client_init(&cfg);
    if (s_client == NULL) { ESP_LOGE(TAG, "client init failed"); return; }
    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    xTaskCreate(publish_task, "carl_ha_mqtt", 4096, NULL, 4, NULL);
    ESP_LOGI(TAG, "HA MQTT bridge → %s (every %ds)",
             CONFIG_CARL_MQTT_HA_BROKER_URI, CONFIG_CARL_MQTT_HA_INTERVAL_SEC);
}

#else  // !CONFIG_CARL_MQTT_HA_ENABLE

void carl_mqtt_bridge_start(void) {}

#endif
