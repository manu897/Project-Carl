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
#include "nvs.h"

#include "time_sync.h"

static const char *TAG = "carl-registry";

#define MAX_NODES 16
// Recent-history ring depth per node. At the normal 30-min cadence this
// covers ~24h; during warning/critical cadence it covers less wall-clock
// time but more detail. Long-term history is owned by Project-Norman.
#define HIST_CAP 48
#define NVS_META_NAMESPACE "carl-nodes"

// --- calibration defaults (mirror the OpenAPI Calibration schema) ---
#define DEF_SOIL_DRY_PCT      25.0f
#define DEF_SOIL_WET_PCT      65.0f
#define DEF_BATTERY_LOW_PCT   15
#define DEF_OFFLINE_AFTER_MIN 30

// History sample: a compact snapshot of the merged latest reading + capture
// time. `flags` records which fields are valid.
enum {
    F_TEMP = 1 << 0, F_HUM = 1 << 1, F_PRESS = 1 << 2,
    F_SOIL = 1 << 3, F_LUX = 1 << 4, F_BAT  = 1 << 5,
};
typedef struct {
    int64_t  t_us;
    float    temp_c, humidity_pct, pressure_hpa, soil_pct, lux;
    uint8_t  battery_pct;
    uint8_t  flags;
} hist_sample_t;

// Persisted per-node metadata (NVS blob, keyed by MAC).
typedef struct {
    char               name[32];
    carl_calibration_t cal;
} node_meta_t;

typedef struct {
    bool               used;
    uint8_t            mac[6];          // little-endian (matches NimBLE)
    uint32_t           last_counter;    // last accepted BTHome replay counter
    int64_t            last_seen_us;    // esp_timer_get_time() at last update
    carl_reading_t     latest;
    node_meta_t        meta;
    hist_sample_t      hist[HIST_CAP];
    uint16_t           hist_head;       // index of next write
    uint16_t           hist_count;      // valid samples (<= HIST_CAP)
} entry_t;

static entry_t s_table[MAX_NODES];
static unsigned s_count = 0;
static SemaphoreHandle_t s_lock = NULL;

// ---------- formatting helpers ----------

// "AA:BB:CC:DD:EE:FF" (uppercase, big-endian display order).
static void mac_to_str(const uint8_t mac6_le[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac6_le[5], mac6_le[4], mac6_le[3],
             mac6_le[2], mac6_le[1], mac6_le[0]);
}

// "node-AABB" — short stable id from the last 2 MAC bytes.
static void mac_to_node_id(const uint8_t mac6_le[6], char out[16]) {
    snprintf(out, 16, "node-%02X%02X", mac6_le[1], mac6_le[0]);
}

// 12-char uppercase hex of the full MAC — used as the NVS metadata key
// (NVS keys must be <= 15 chars).
static void mac_to_nvs_key(const uint8_t mac6_le[6], char out[13]) {
    snprintf(out, 13, "%02X%02X%02X%02X%02X%02X",
             mac6_le[5], mac6_le[4], mac6_le[3],
             mac6_le[2], mac6_le[1], mac6_le[0]);
}

static carl_calibration_t default_cal(void) {
    carl_calibration_t c = {
        .soil_dry_pct      = DEF_SOIL_DRY_PCT,
        .soil_wet_pct      = DEF_SOIL_WET_PCT,
        .battery_low_pct   = DEF_BATTERY_LOW_PCT,
        .offline_after_min = DEF_OFFLINE_AFTER_MIN,
    };
    return c;
}

// ---------- NVS metadata persistence ----------

static bool meta_load_nvs(const uint8_t mac6_le[6], node_meta_t *out) {
    nvs_handle_t h;
    if (nvs_open(NVS_META_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return false;
    char key[13];
    mac_to_nvs_key(mac6_le, key);
    size_t len = sizeof(*out);
    esp_err_t err = nvs_get_blob(h, key, out, &len);
    nvs_close(h);
    return err == ESP_OK && len == sizeof(*out);
}

static void meta_save_nvs(const uint8_t mac6_le[6], const node_meta_t *m) {
    nvs_handle_t h;
    if (nvs_open(NVS_META_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "nvs_open failed — metadata not persisted");
        return;
    }
    char key[13];
    mac_to_nvs_key(mac6_le, key);
    esp_err_t err = nvs_set_blob(h, key, m, sizeof(*m));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) ESP_LOGW(TAG, "metadata NVS write failed: %s", esp_err_to_name(err));
}

static void meta_erase_nvs(const uint8_t mac6_le[6]) {
    nvs_handle_t h;
    if (nvs_open(NVS_META_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    char key[13];
    mac_to_nvs_key(mac6_le, key);
    nvs_erase_key(h, key);
    nvs_commit(h);
    nvs_close(h);
}

// ---------- table helpers (call with lock held) ----------

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
        s_count++;
        // Seed metadata: NVS if provisioned earlier, else sensible defaults.
        if (!meta_load_nvs(mac6_le, &first_free->meta)) {
            first_free->meta.cal = default_cal();
            mac_to_node_id(mac6_le, first_free->meta.name);  // name defaults to id
        }
    }
    return first_free;
}

static entry_t *find_by_mac(const uint8_t mac6_le[6]) {
    for (int i = 0; i < MAX_NODES; ++i) {
        if (s_table[i].used && memcmp(s_table[i].mac, mac6_le, 6) == 0) {
            return &s_table[i];
        }
    }
    return NULL;
}

static entry_t *find_by_id(const char *id) {
    if (id == NULL) return NULL;
    for (int i = 0; i < MAX_NODES; ++i) {
        if (!s_table[i].used) continue;
        char this_id[16];
        mac_to_node_id(s_table[i].mac, this_id);
        if (strcmp(this_id, id) == 0) return &s_table[i];
    }
    return NULL;
}

static bool node_is_online(const entry_t *e, int64_t now_us) {
    const int64_t timeout_us =
        (int64_t)e->meta.cal.offline_after_min * 60LL * 1000000LL;
    return (now_us - e->last_seen_us) < timeout_us;
}

// Push a snapshot of the merged latest reading into the history ring.
static void record_history(entry_t *e, int64_t t_us) {
    hist_sample_t *s = &e->hist[e->hist_head];
    memset(s, 0, sizeof(*s));
    s->t_us = t_us;
    if (e->latest.temp_ok)     { s->temp_c = e->latest.temp_c;             s->flags |= F_TEMP; }
    if (e->latest.humidity_ok) { s->humidity_pct = e->latest.humidity_pct; s->flags |= F_HUM; }
    if (e->latest.pressure_ok) { s->pressure_hpa = e->latest.pressure_hpa; s->flags |= F_PRESS; }
    if (e->latest.soil_ok)     { s->soil_pct = e->latest.soil_pct;         s->flags |= F_SOIL; }
    if (e->latest.lux_ok)      { s->lux = e->latest.lux;                   s->flags |= F_LUX; }
    if (e->latest.battery_ok)  { s->battery_pct = e->latest.battery_pct;   s->flags |= F_BAT; }

    e->hist_head = (e->hist_head + 1) % HIST_CAP;
    if (e->hist_count < HIST_CAP) e->hist_count++;
}

// ---------- JSON builders (call with lock held) ----------

static void add_num_or_null(cJSON *obj, const char *k, bool ok, double v) {
    if (ok) cJSON_AddNumberToObject(obj, k, v);
    else    cJSON_AddNullToObject(obj, k);
}

static cJSON *reading_to_json(const carl_reading_t *r, const char *ts) {
    cJSON *reading = cJSON_CreateObject();
    cJSON_AddStringToObject(reading, "ts", ts);
    add_num_or_null(reading, "temperature_c",   r->temp_ok,     r->temp_c);
    add_num_or_null(reading, "humidity_pct",    r->humidity_ok, r->humidity_pct);
    add_num_or_null(reading, "pressure_hpa",    r->pressure_ok, r->pressure_hpa);
    add_num_or_null(reading, "soil_pct",        r->soil_ok,     r->soil_pct);
    add_num_or_null(reading, "illuminance_lux", r->lux_ok,      r->lux);
    add_num_or_null(reading, "battery_pct",     r->battery_ok,  r->battery_pct);
    return reading;
}

static cJSON *node_to_json(const entry_t *e, int64_t now_us) {
    char mac_str[18];   mac_to_str(e->mac, mac_str);
    char id_str[16];    mac_to_node_id(e->mac, id_str);
    char ts_buf[32];    carl_time_format_event(e->last_seen_us, ts_buf, sizeof(ts_buf));

    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "id",   id_str);
    cJSON_AddStringToObject(node, "mac",  mac_str);
    cJSON_AddStringToObject(node, "name", e->meta.name);
    cJSON_AddStringToObject(node, "last_seen", ts_buf);
    cJSON_AddBoolToObject  (node, "online", node_is_online(e, now_us));
    if (e->latest.battery_ok)
        cJSON_AddNumberToObject(node, "battery_pct", e->latest.battery_pct);
    else
        cJSON_AddNullToObject(node, "battery_pct");

    cJSON_AddItemToObject(node, "latest", reading_to_json(&e->latest, ts_buf));

    cJSON *cal = cJSON_CreateObject();
    cJSON_AddNumberToObject(cal, "soil_dry_pct",          e->meta.cal.soil_dry_pct);
    cJSON_AddNumberToObject(cal, "soil_wet_pct",          e->meta.cal.soil_wet_pct);
    cJSON_AddNumberToObject(cal, "battery_low_pct",       e->meta.cal.battery_low_pct);
    cJSON_AddNumberToObject(cal, "offline_after_minutes", e->meta.cal.offline_after_min);
    cJSON_AddItemToObject(node, "calibration", cal);

    return node;
}

// ---------- public API ----------

void carl_node_registry_init(void) {
    if (s_lock == NULL) s_lock = xSemaphoreCreateMutex();
    memset(s_table, 0, sizeof(s_table));
    s_count = 0;
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
    // Replay protection: BTHome counter must strictly increase. last_counter
    // == 0 means "first accepted ad after boot", so accept any counter then.
    if (e->last_counter != 0 && counter <= e->last_counter) {
        ESP_LOGD(TAG, "stale counter %" PRIu32 " <= last %" PRIu32 " — drop",
                 counter, e->last_counter);
        xSemaphoreGive(s_lock);
        return;
    }
    e->last_counter = counter;
    e->last_seen_us = esp_timer_get_time();

    // Node alternates which fields it sends per ad (T/H/P one cycle,
    // soil/lux the next), so we OR new fields onto the existing reading.
    if (r->temp_ok)     { e->latest.temp_ok     = true; e->latest.temp_c       = r->temp_c; }
    if (r->humidity_ok) { e->latest.humidity_ok = true; e->latest.humidity_pct = r->humidity_pct; }
    if (r->pressure_ok) { e->latest.pressure_ok = true; e->latest.pressure_hpa = r->pressure_hpa; }
    if (r->soil_ok)     { e->latest.soil_ok     = true; e->latest.soil_pct     = r->soil_pct; }
    if (r->lux_ok)      { e->latest.lux_ok      = true; e->latest.lux          = r->lux; }
    if (r->battery_ok)  { e->latest.battery_ok  = true; e->latest.battery_pct  = r->battery_pct; }
    e->latest.packet_id = r->packet_id;

    record_history(e, e->last_seen_us);

    xSemaphoreGive(s_lock);
}

unsigned carl_node_registry_count(void) {
    return s_count;
}

static char *empty_array(void) {
    char *empty = (char *)malloc(3);
    if (empty) strcpy(empty, "[]");
    return empty;
}

char *carl_node_registry_render_json(void) {
    if (s_lock == NULL) return empty_array();
    xSemaphoreTake(s_lock, portMAX_DELAY);
    cJSON *arr = cJSON_CreateArray();
    const int64_t now_us = esp_timer_get_time();
    for (int i = 0; i < MAX_NODES; ++i) {
        if (!s_table[i].used) continue;
        cJSON_AddItemToArray(arr, node_to_json(&s_table[i], now_us));
    }
    char *body = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    xSemaphoreGive(s_lock);
    return body ? body : empty_array();
}

char *carl_node_registry_get_json(const char *id) {
    if (s_lock == NULL) return NULL;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    entry_t *e = find_by_id(id);
    char *body = NULL;
    if (e != NULL) {
        cJSON *node = node_to_json(e, esp_timer_get_time());
        body = cJSON_PrintUnformatted(node);
        cJSON_Delete(node);
    }
    xSemaphoreGive(s_lock);
    return body;
}

char *carl_node_registry_history_json(const char *id, const char *range) {
    if (s_lock == NULL) return NULL;

    // Map range → seconds; default + unknown → 24h.
    int64_t range_s = 24LL * 3600;
    const char *range_norm = "24h";
    if (range != NULL) {
        if (strcmp(range, "7d") == 0)       { range_s = 7LL * 24 * 3600;  range_norm = "7d"; }
        else if (strcmp(range, "30d") == 0) { range_s = 30LL * 24 * 3600; range_norm = "30d"; }
    }

    xSemaphoreTake(s_lock, portMAX_DELAY);
    entry_t *e = find_by_id(id);
    if (e == NULL) {
        xSemaphoreGive(s_lock);
        return NULL;
    }

    char id_str[16]; mac_to_node_id(e->mac, id_str);
    const int64_t now_us       = esp_timer_get_time();
    const int64_t cutoff_us    = now_us - range_s * 1000000LL;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "node_id", id_str);
    cJSON_AddStringToObject(root, "range", range_norm);
    cJSON *samples = cJSON_AddArrayToObject(root, "samples");

    // Walk oldest → newest.
    for (uint16_t n = 0; n < e->hist_count; ++n) {
        uint16_t idx = (e->hist_head + HIST_CAP - e->hist_count + n) % HIST_CAP;
        const hist_sample_t *s = &e->hist[idx];
        if (s->t_us < cutoff_us) continue;

        char ts_buf[32];
        carl_time_format_event(s->t_us, ts_buf, sizeof(ts_buf));
        cJSON *sample = cJSON_CreateObject();
        cJSON_AddStringToObject(sample, "ts", ts_buf);
        add_num_or_null(sample, "temperature_c",   s->flags & F_TEMP,  s->temp_c);
        add_num_or_null(sample, "humidity_pct",    s->flags & F_HUM,   s->humidity_pct);
        add_num_or_null(sample, "pressure_hpa",    s->flags & F_PRESS, s->pressure_hpa);
        add_num_or_null(sample, "soil_pct",        s->flags & F_SOIL,  s->soil_pct);
        add_num_or_null(sample, "illuminance_lux", s->flags & F_LUX,   s->lux);
        add_num_or_null(sample, "battery_pct",     s->flags & F_BAT,   s->battery_pct);
        cJSON_AddItemToArray(samples, sample);
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    xSemaphoreGive(s_lock);
    return body;
}

bool carl_node_registry_update_meta(const char *id,
                                     const char *name,
                                     const carl_calibration_t *cal) {
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    entry_t *e = find_by_id(id);
    if (e == NULL) {
        xSemaphoreGive(s_lock);
        return false;
    }
    if (name != NULL) {
        strncpy(e->meta.name, name, sizeof(e->meta.name) - 1);
        e->meta.name[sizeof(e->meta.name) - 1] = '\0';
    }
    if (cal != NULL) e->meta.cal = *cal;
    meta_save_nvs(e->mac, &e->meta);
    xSemaphoreGive(s_lock);
    return true;
}

void carl_node_registry_set_meta_by_mac(const uint8_t mac6_le[6],
                                         const char *name,
                                         const carl_calibration_t *cal) {
    if (s_lock == NULL) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);

    // Start from whatever is persisted (or defaults), then layer the update.
    node_meta_t m;
    if (!meta_load_nvs(mac6_le, &m)) {
        m.cal = default_cal();
        mac_to_node_id(mac6_le, m.name);
    }
    if (name != NULL) {
        strncpy(m.name, name, sizeof(m.name) - 1);
        m.name[sizeof(m.name) - 1] = '\0';
    }
    if (cal != NULL) m.cal = *cal;
    meta_save_nvs(mac6_le, &m);

    // If the node has already been heard, apply immediately. Otherwise the
    // metadata stays in NVS and is picked up when the node is first heard
    // (find_or_alloc loads it) — no phantom registry entry is created.
    entry_t *e = find_by_mac(mac6_le);
    if (e != NULL) e->meta = m;

    xSemaphoreGive(s_lock);
}

bool carl_node_registry_remove(const char *id, uint8_t out_mac_le[6]) {
    if (s_lock == NULL) return false;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    entry_t *e = find_by_id(id);
    if (e == NULL) {
        xSemaphoreGive(s_lock);
        return false;
    }
    if (out_mac_le != NULL) memcpy(out_mac_le, e->mac, 6);
    meta_erase_nvs(e->mac);
    memset(e, 0, sizeof(*e));
    if (s_count > 0) s_count--;
    xSemaphoreGive(s_lock);
    return true;
}
