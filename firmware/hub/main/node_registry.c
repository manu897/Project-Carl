#include "node_registry.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "carl-registry";

#define MAX_NODES 16
// Treat a node as offline if we haven't heard from it within this window.
// Sensor node's normal cadence is 30 min (Severity::OK), so 90 min is a
// generous "missed three samples in a row" threshold.
#define ONLINE_TIMEOUT_US (90LL * 60LL * 1000000LL)

typedef struct {
    bool            used;
    uint8_t         mac[6];          // little-endian (matches NimBLE)
    uint32_t        last_counter;    // last accepted BTHome replay counter
    int64_t         last_seen_us;    // esp_timer_get_time() at last update
    carl_reading_t  latest;
} entry_t;

static entry_t s_table[MAX_NODES];
static SemaphoreHandle_t s_lock = NULL;

void carl_node_registry_init(void) {
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    memset(s_table, 0, sizeof(s_table));
}

// MAC formatting: little-endian buffer in, "AA:BB:CC:DD:EE:FF" out
// (uppercase, big-endian display order — matches every other tool).
static void mac_to_str(const uint8_t mac6_le[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac6_le[5], mac6_le[4], mac6_le[3],
             mac6_le[2], mac6_le[1], mac6_le[0]);
}

// "node-AABB" — short stable id derived from the last 2 MAC bytes; used as
// the hub-assigned id field in the OpenAPI Node schema until the user
// names their plant via the dashboard (Phase 3d).
static void mac_to_node_id(const uint8_t mac6_le[6], char out[16]) {
    snprintf(out, 16, "node-%02X%02X", mac6_le[1], mac6_le[0]);
}

static entry_t *find_or_alloc(const uint8_t mac6_le[6]) {
    entry_t *first_free = NULL;
    for (int i = 0; i < MAX_NODES; ++i) {
        if (s_table[i].used && memcmp(s_table[i].mac, mac6_le, 6) == 0) {
            return &s_table[i];
        }
        if (!s_table[i].used && first_free == NULL) {
            first_free = &s_table[i];
        }
    }
    if (first_free != NULL) {
        memset(first_free, 0, sizeof(*first_free));
        memcpy(first_free->mac, mac6_le, 6);
        first_free->used = true;
    }
    return first_free;
}

void carl_node_registry_update(const uint8_t mac6_le[6],
                               uint32_t counter,
                               const carl_reading_t *r) {
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    entry_t *e = find_or_alloc(mac6_le);
    if (e == NULL) {
        ESP_LOGW(TAG, "node table full, dropping advertisement");
        xSemaphoreGive(s_lock);
        return;
    }
    // Replay protection: BTHome counter must strictly increase. The very
    // first accepted advertisement after boot also has last_counter==0,
    // which is why we also accept any counter when last_counter is 0.
    if (e->last_counter != 0 && counter <= e->last_counter) {
        ESP_LOGW(TAG, "stale counter %" PRIu32 " <= last %" PRIu32 " — drop",
                 counter, e->last_counter);
        xSemaphoreGive(s_lock);
        return;
    }
    e->last_counter = counter;
    e->last_seen_us = esp_timer_get_time();

    // Merge new fields onto existing reading — sensor node alternates which
    // fields it sends per advertisement (T/H/P one cycle, soil/lux the
    // next), so we OR rather than replace.
    if (r->temp_ok)     { e->latest.temp_ok     = true; e->latest.temp_c       = r->temp_c; }
    if (r->humidity_ok) { e->latest.humidity_ok = true; e->latest.humidity_pct = r->humidity_pct; }
    if (r->pressure_ok) { e->latest.pressure_ok = true; e->latest.pressure_hpa = r->pressure_hpa; }
    if (r->soil_ok)     { e->latest.soil_ok     = true; e->latest.soil_pct     = r->soil_pct; }
    if (r->lux_ok)      { e->latest.lux_ok      = true; e->latest.lux          = r->lux; }
    if (r->battery_ok)  { e->latest.battery_ok  = true; e->latest.battery_pct  = r->battery_pct; }
    e->latest.packet_id = r->packet_id;

    xSemaphoreGive(s_lock);
}

char *carl_node_registry_render_json(void) {
    if (s_lock == NULL) {
        char *empty = (char *)malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *arr = cJSON_CreateArray();
    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < MAX_NODES; ++i) {
        if (!s_table[i].used) continue;
        const entry_t *e = &s_table[i];

        char mac_str[18];   mac_to_str(e->mac, mac_str);
        char id_str[16];    mac_to_node_id(e->mac, id_str);

        cJSON *node = cJSON_CreateObject();
        cJSON_AddStringToObject(node, "id",        id_str);
        cJSON_AddStringToObject(node, "mac",       mac_str);
        // Phase 3d wires per-node names from NVS; for now derive from id.
        cJSON_AddStringToObject(node, "name",      id_str);
        cJSON_AddBoolToObject  (node, "online",
            (now_us - e->last_seen_us) < ONLINE_TIMEOUT_US);
        // last_seen as seconds-since-epoch isn't available without an RTC
        // sync; emit relative seconds-ago for now (Phase 3d adds SNTP).
        char last_seen_buf[32];
        snprintf(last_seen_buf, sizeof(last_seen_buf), "%lld",
                 (long long)((now_us - e->last_seen_us) / 1000000));
        cJSON_AddStringToObject(node, "last_seen", last_seen_buf);
        if (e->latest.battery_ok) {
            cJSON_AddNumberToObject(node, "battery_pct", e->latest.battery_pct);
        } else {
            cJSON_AddNullToObject  (node, "battery_pct");
        }

        cJSON *reading = cJSON_CreateObject();
        cJSON_AddStringToObject(reading, "ts", last_seen_buf);
        if (e->latest.temp_ok)
            cJSON_AddNumberToObject(reading, "temperature_c", e->latest.temp_c);
        else
            cJSON_AddNullToObject(reading, "temperature_c");
        if (e->latest.humidity_ok)
            cJSON_AddNumberToObject(reading, "humidity_pct", e->latest.humidity_pct);
        else
            cJSON_AddNullToObject(reading, "humidity_pct");
        if (e->latest.pressure_ok)
            cJSON_AddNumberToObject(reading, "pressure_hpa", e->latest.pressure_hpa);
        else
            cJSON_AddNullToObject(reading, "pressure_hpa");
        if (e->latest.soil_ok)
            cJSON_AddNumberToObject(reading, "soil_pct", e->latest.soil_pct);
        else
            cJSON_AddNullToObject(reading, "soil_pct");
        if (e->latest.lux_ok)
            cJSON_AddNumberToObject(reading, "illuminance_lux", e->latest.lux);
        else
            cJSON_AddNullToObject(reading, "illuminance_lux");
        if (e->latest.battery_ok)
            cJSON_AddNumberToObject(reading, "battery_pct", e->latest.battery_pct);
        else
            cJSON_AddNullToObject(reading, "battery_pct");

        cJSON_AddItemToObject(node, "latest", reading);
        cJSON_AddItemToArray(arr, node);
    }
    char *body = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    xSemaphoreGive(s_lock);
    if (body == NULL) {
        char *empty = (char *)malloc(3);
        if (empty) strcpy(empty, "[]");
        return empty;
    }
    return body;
}
