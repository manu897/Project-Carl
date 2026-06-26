#include "http_api.h"

#include <stdlib.h>
#include <string.h>

#include <stdio.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_littlefs.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "key_store.h"
#include "node_registry.h"
#include "time_sync.h"
#include "wifi_sta.h"

static const char *TAG = "carl-api";

#define MAX_POST_BODY 512

// Set while the hub is in SoftAP onboarding — the static handler then serves
// setup.html for every page request so captive-portal probes hit the form.
static bool s_setup_mode = false;

void carl_http_set_setup_mode(bool on) { s_setup_mode = on; }

// ---------- helpers ----------

static void send_json(httpd_req_t *req, const char *body, size_t len) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, body, len);
}

static void send_json_owned(httpd_req_t *req, char *body) {
    send_json(req, body, strlen(body));
    free(body);
}

static void send_error(httpd_req_t *req, const char *status,
                       const char *code, const char *msg) {
    httpd_resp_set_status(req, status);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "code", code);
    cJSON_AddStringToObject(root, "message", msg);
    char *body = cJSON_PrintUnformatted(root);
    send_json(req, body, strlen(body));
    free(body);
    cJSON_Delete(root);
}

static void mac_le_to_str(const uint8_t mac6_le[6], char out[18]) {
    snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac6_le[5], mac6_le[4], mac6_le[3],
             mac6_le[2], mac6_le[1], mac6_le[0]);
}

static int read_post_body(httpd_req_t *req, char *buf, size_t cap) {
    int total = req->content_len;
    if (total <= 0)            { send_error(req, "400 Bad Request", "empty_body", "empty body"); return -1; }
    if ((size_t)total >= cap)  { send_error(req, "400 Bad Request", "body_too_large", "body too large"); return -1; }
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, buf + received, total - received);
        if (ret <= 0) {
            if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
            send_error(req, "500 Internal Server Error", "recv_error", "recv error");
            return -1;
        }
        received += ret;
    }
    buf[received] = '\0';
    return received;
}

// Parse "/api/nodes/{id}" or "/api/nodes/{id}/history[?range=..]" — extract the
// id segment and flag whether the /history sub-resource was requested.
static bool parse_node_path(const char *uri, char *id_out, size_t id_cap,
                            bool *is_history) {
    const char *prefix = "/api/nodes/";
    const size_t plen = strlen(prefix);
    if (strncmp(uri, prefix, plen) != 0) return false;
    const char *rest = uri + plen;
    const char *q = strchr(rest, '?');
    const size_t rest_len = q ? (size_t)(q - rest) : strlen(rest);
    const char *slash = memchr(rest, '/', rest_len);
    const size_t id_len = slash ? (size_t)(slash - rest) : rest_len;
    if (id_len == 0 || id_len >= id_cap) return false;
    memcpy(id_out, rest, id_len);
    id_out[id_len] = '\0';
    *is_history = false;
    if (slash) {
        const char *seg = slash + 1;
        const size_t seg_len = rest_len - (id_len + 1);
        if (seg_len == 7 && strncmp(seg, "history", 7) == 0) *is_history = true;
    }
    return true;
}

// Pull calibration fields out of a JSON object, seeding from defaults so a
// present `calibration` object is treated as a full replacement.
static void parse_calibration(const cJSON *jcal, carl_calibration_t *cal) {
    const cJSON *v;
    if ((v = cJSON_GetObjectItem(jcal, "soil_dry_pct"))          && cJSON_IsNumber(v)) cal->soil_dry_pct = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(jcal, "soil_wet_pct"))          && cJSON_IsNumber(v)) cal->soil_wet_pct = (float)v->valuedouble;
    if ((v = cJSON_GetObjectItem(jcal, "battery_low_pct"))       && cJSON_IsNumber(v)) cal->battery_low_pct = (uint8_t)v->valueint;
    if ((v = cJSON_GetObjectItem(jcal, "offline_after_minutes")) && cJSON_IsNumber(v)) cal->offline_after_min = (uint16_t)v->valueint;
}

static carl_calibration_t default_calibration(void) {
    carl_calibration_t c = { 25.0f, 65.0f, 15, 30 };
    return c;
}

// ---------- GET /api/health ----------

static esp_err_t handle_health(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "hub_id", CONFIG_CARL_MDNS_HOSTNAME);
    cJSON_AddStringToObject(root, "version", (app != NULL) ? app->version : "unknown");
    cJSON_AddNumberToObject(root, "uptime_s", (double)(esp_timer_get_time() / 1000000));
    cJSON_AddNumberToObject(root, "node_count", (double)carl_node_registry_count());
    int rssi;
    if (carl_wifi_rssi_dbm(&rssi)) cJSON_AddNumberToObject(root, "wifi_rssi_dbm", rssi);
    else                           cJSON_AddNullToObject(root, "wifi_rssi_dbm");

    send_json_owned(req, cJSON_PrintUnformatted(root));
    cJSON_Delete(root);
    return ESP_OK;
}

// ---------- /api/nodes (collection) ----------

static esp_err_t handle_nodes_get(httpd_req_t *req) {
    char *body = carl_node_registry_render_json();
    if (body == NULL) { httpd_resp_send_500(req); return ESP_FAIL; }
    send_json_owned(req, body);
    return ESP_OK;
}

// POST /api/nodes — provision: {mac, key_hex, name, calibration?}
static esp_err_t handle_nodes_post(httpd_req_t *req) {
    char buf[MAX_POST_BODY];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_OK;

    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) { send_error(req, "400 Bad Request", "bad_json", "invalid JSON"); return ESP_OK; }

    const cJSON *j_mac  = cJSON_GetObjectItem(root, "mac");
    const cJSON *j_key  = cJSON_GetObjectItem(root, "key_hex");
    const cJSON *j_name = cJSON_GetObjectItem(root, "name");
    if (!cJSON_IsString(j_mac) || !cJSON_IsString(j_key)) {
        send_error(req, "400 Bad Request", "bad_request", "need \"mac\" and \"key_hex\" strings");
        cJSON_Delete(root); return ESP_OK;
    }

    uint8_t mac_le[6], key[16];
    if (!carl_key_store_parse_mac(j_mac->valuestring, mac_le)) {
        send_error(req, "400 Bad Request", "bad_mac", "invalid MAC — use AA:BB:CC:DD:EE:FF");
        cJSON_Delete(root); return ESP_OK;
    }
    if (!carl_key_store_parse_key(j_key->valuestring, key)) {
        send_error(req, "400 Bad Request", "bad_key", "key_hex must be 32 hex chars");
        cJSON_Delete(root); return ESP_OK;
    }

    // 409 if this MAC is already provisioned.
    if (carl_key_store_lookup(mac_le) != NULL) {
        send_error(req, "409 Conflict", "already_provisioned", "node already provisioned");
        cJSON_Delete(root); return ESP_OK;
    }
    if (!carl_key_store_add(mac_le, key)) {
        send_error(req, "507 Insufficient Storage", "store_full", "key store full");
        cJSON_Delete(root); return ESP_OK;
    }

    const char *name = cJSON_IsString(j_name) ? j_name->valuestring : NULL;
    carl_calibration_t cal = default_calibration();
    const cJSON *j_cal = cJSON_GetObjectItem(root, "calibration");
    if (cJSON_IsObject(j_cal)) parse_calibration(j_cal, &cal);
    carl_node_registry_set_meta_by_mac(mac_le, name, &cal);

    // Spec-shaped Node placeholder. The node hasn't been heard yet, so
    // last_seen/latest carry "now" with null readings (the Node schema marks
    // both required + non-nullable) and online=false until the first ad.
    char mac_str[18]; mac_le_to_str(mac_le, mac_str);
    char id_str[16];  snprintf(id_str, sizeof(id_str), "node-%02X%02X", mac_le[1], mac_le[0]);
    char now_iso[32]; carl_time_format_event(esp_timer_get_time(), now_iso, sizeof(now_iso));
    cJSON *node = cJSON_CreateObject();
    cJSON_AddStringToObject(node, "id", id_str);
    cJSON_AddStringToObject(node, "mac", mac_str);
    cJSON_AddStringToObject(node, "name", name ? name : id_str);
    cJSON_AddStringToObject(node, "last_seen", now_iso);
    cJSON_AddBoolToObject(node, "online", false);
    cJSON_AddNullToObject(node, "battery_pct");
    cJSON *latest = cJSON_CreateObject();
    cJSON_AddStringToObject(latest, "ts", now_iso);
    cJSON_AddNullToObject(latest, "temperature_c");
    cJSON_AddNullToObject(latest, "humidity_pct");
    cJSON_AddNullToObject(latest, "pressure_hpa");
    cJSON_AddNullToObject(latest, "soil_pct");
    cJSON_AddNullToObject(latest, "illuminance_lux");
    cJSON_AddNullToObject(latest, "battery_pct");
    cJSON_AddItemToObject(node, "latest", latest);

    httpd_resp_set_status(req, "201 Created");
    send_json_owned(req, cJSON_PrintUnformatted(node));
    cJSON_Delete(node);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "POST /api/nodes — provisioned %s (%s)", mac_str, name ? name : id_str);
    return ESP_OK;
}

// ---------- /api/nodes/* (single + history) ----------

static esp_err_t serve_node_or_history(httpd_req_t *req, const char *id, bool is_history) {
    if (is_history) {
        char range[8] = "24h";
        size_t qlen = httpd_req_get_url_query_len(req) + 1;
        if (qlen > 1 && qlen < 128) {
            char q[128];
            if (httpd_req_get_url_query_str(req, q, qlen) == ESP_OK) {
                char val[8];
                if (httpd_query_key_value(q, "range", val, sizeof(val)) == ESP_OK) {
                    strncpy(range, val, sizeof(range) - 1);
                    range[sizeof(range) - 1] = '\0';
                }
            }
        }
        char *body = carl_node_registry_history_json(id, range);
        if (body == NULL) { send_error(req, "404 Not Found", "not_found", "no such node"); return ESP_OK; }
        send_json_owned(req, body);
        return ESP_OK;
    }
    char *body = carl_node_registry_get_json(id);
    if (body == NULL) { send_error(req, "404 Not Found", "not_found", "no such node"); return ESP_OK; }
    send_json_owned(req, body);
    return ESP_OK;
}

static esp_err_t handle_node_get(httpd_req_t *req) {
    char id[16]; bool is_history;
    if (!parse_node_path(req->uri, id, sizeof(id), &is_history)) {
        send_error(req, "400 Bad Request", "bad_path", "malformed node path");
        return ESP_OK;
    }
    return serve_node_or_history(req, id, is_history);
}

// PATCH /api/nodes/{id} — {name?, calibration?}
static esp_err_t handle_node_patch(httpd_req_t *req) {
    char id[16]; bool is_history;
    if (!parse_node_path(req->uri, id, sizeof(id), &is_history) || is_history) {
        send_error(req, "400 Bad Request", "bad_path", "malformed node path");
        return ESP_OK;
    }
    char buf[MAX_POST_BODY];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_OK;
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) { send_error(req, "400 Bad Request", "bad_json", "invalid JSON"); return ESP_OK; }

    const cJSON *j_name = cJSON_GetObjectItem(root, "name");
    const char *name = cJSON_IsString(j_name) ? j_name->valuestring : NULL;

    carl_calibration_t cal = default_calibration();
    const cJSON *j_cal = cJSON_GetObjectItem(root, "calibration");
    bool has_cal = cJSON_IsObject(j_cal);
    if (has_cal) parse_calibration(j_cal, &cal);

    if (!carl_node_registry_update_meta(id, name, has_cal ? &cal : NULL)) {
        send_error(req, "404 Not Found", "not_found", "no such node");
        cJSON_Delete(root); return ESP_OK;
    }
    cJSON_Delete(root);

    // Echo back the updated node.
    char *body = carl_node_registry_get_json(id);
    if (body == NULL) { httpd_resp_send_500(req); return ESP_FAIL; }
    send_json_owned(req, body);
    ESP_LOGI(TAG, "PATCH /api/nodes/%s", id);
    return ESP_OK;
}

// DELETE /api/nodes/{id} — remove node + purge its AES key.
static esp_err_t handle_node_delete(httpd_req_t *req) {
    char id[16]; bool is_history;
    if (!parse_node_path(req->uri, id, sizeof(id), &is_history) || is_history) {
        send_error(req, "400 Bad Request", "bad_path", "malformed node path");
        return ESP_OK;
    }
    uint8_t mac_le[6];
    if (!carl_node_registry_remove(id, mac_le)) {
        send_error(req, "404 Not Found", "not_found", "no such node");
        return ESP_OK;
    }
    carl_key_store_remove(mac_le);  // purge the key too (best-effort)

    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, NULL, 0);
    ESP_LOGI(TAG, "DELETE /api/nodes/%s", id);
    return ESP_OK;
}

// ---------- /api/keys (hub-internal convenience aliases) ----------

typedef struct { cJSON *arr; } keys_list_ctx_t;

static bool keys_list_cb(const uint8_t mac6_le[6], void *user) {
    keys_list_ctx_t *ctx = (keys_list_ctx_t *)user;
    char mac_str[18]; mac_le_to_str(mac6_le, mac_str);
    cJSON *obj = cJSON_CreateObject();
    cJSON_AddStringToObject(obj, "mac", mac_str);
    cJSON_AddItemToArray(ctx->arr, obj);
    return true;
}

static esp_err_t handle_keys_list(httpd_req_t *req) {
    keys_list_ctx_t ctx; ctx.arr = cJSON_CreateArray();
    carl_key_store_foreach(keys_list_cb, &ctx);
    send_json_owned(req, cJSON_PrintUnformatted(ctx.arr));
    cJSON_Delete(ctx.arr);
    return ESP_OK;
}

// POST /api/keys — lightweight provisioning: {mac, key} (no name/cal).
static esp_err_t handle_keys_post(httpd_req_t *req) {
    char buf[MAX_POST_BODY];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_OK;
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) { send_error(req, "400 Bad Request", "bad_json", "invalid JSON"); return ESP_OK; }

    const cJSON *j_mac = cJSON_GetObjectItem(root, "mac");
    const cJSON *j_key = cJSON_GetObjectItem(root, "key");
    if (!cJSON_IsString(j_mac) || !cJSON_IsString(j_key)) {
        send_error(req, "400 Bad Request", "bad_request", "need \"mac\" and \"key\" strings");
        cJSON_Delete(root); return ESP_OK;
    }
    uint8_t mac_le[6], key[16];
    if (!carl_key_store_parse_mac(j_mac->valuestring, mac_le)) {
        send_error(req, "400 Bad Request", "bad_mac", "invalid MAC");
        cJSON_Delete(root); return ESP_OK;
    }
    if (!carl_key_store_parse_key(j_key->valuestring, key)) {
        send_error(req, "400 Bad Request", "bad_key", "key must be 32 hex chars");
        cJSON_Delete(root); return ESP_OK;
    }
    if (!carl_key_store_add(mac_le, key)) {
        send_error(req, "507 Insufficient Storage", "store_full", "key store full");
        cJSON_Delete(root); return ESP_OK;
    }
    cJSON *resp = cJSON_CreateObject();
    cJSON_AddBoolToObject(resp, "ok", true);
    cJSON_AddStringToObject(resp, "mac", j_mac->valuestring);
    cJSON_AddNumberToObject(resp, "provisioned_keys", (double)carl_key_store_count());
    send_json_owned(req, cJSON_PrintUnformatted(resp));
    cJSON_Delete(resp);
    cJSON_Delete(root);
    ESP_LOGI(TAG, "POST /api/keys — provisioned %s", j_mac->valuestring);
    return ESP_OK;
}

// DELETE /api/keys/{mac}
static esp_err_t handle_keys_delete(httpd_req_t *req) {
    const char *prefix = "/api/keys/";
    const size_t prefix_len = strlen(prefix);
    if (strlen(req->uri) <= prefix_len) {
        send_error(req, "400 Bad Request", "bad_path", "missing MAC in path");
        return ESP_OK;
    }
    const char *mac_str = req->uri + prefix_len;
    size_t mac_len = strlen(mac_str);
    const char *q = strchr(mac_str, '?');
    if (q) mac_len = (size_t)(q - mac_str);
    char mac_buf[18];
    if (mac_len != 17 || mac_len >= sizeof(mac_buf)) {
        send_error(req, "400 Bad Request", "bad_mac", "invalid MAC");
        return ESP_OK;
    }
    memcpy(mac_buf, mac_str, mac_len); mac_buf[mac_len] = '\0';
    uint8_t mac_le[6];
    if (!carl_key_store_parse_mac(mac_buf, mac_le)) {
        send_error(req, "400 Bad Request", "bad_mac", "invalid MAC format");
        return ESP_OK;
    }
    if (!carl_key_store_remove(mac_le)) {
        send_error(req, "404 Not Found", "not_found", "MAC not provisioned");
        return ESP_OK;
    }
    httpd_resp_set_status(req, "204 No Content");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, NULL, 0);
    ESP_LOGI(TAG, "DELETE /api/keys/%s", mac_buf);
    return ESP_OK;
}

// ---------- POST /api/setup/wifi (captive portal) ----------

static void reboot_task(void *arg) {
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(1200));  // let the HTTP response flush first
    ESP_LOGI(TAG, "rebooting to join home Wi-Fi");
    esp_restart();
}

static esp_err_t handle_setup_wifi(httpd_req_t *req) {
    char buf[MAX_POST_BODY];
    if (read_post_body(req, buf, sizeof(buf)) < 0) return ESP_OK;
    cJSON *root = cJSON_Parse(buf);
    if (root == NULL) { send_error(req, "400 Bad Request", "bad_json", "invalid JSON"); return ESP_OK; }

    const cJSON *j_ssid = cJSON_GetObjectItem(root, "ssid");
    const cJSON *j_psk  = cJSON_GetObjectItem(root, "psk");
    if (!cJSON_IsString(j_ssid) || j_ssid->valuestring[0] == '\0') {
        send_error(req, "400 Bad Request", "bad_ssid", "ssid required");
        cJSON_Delete(root); return ESP_OK;
    }
    const char *psk = cJSON_IsString(j_psk) ? j_psk->valuestring : "";
    if (!carl_wifi_save_creds(j_ssid->valuestring, psk)) {
        send_error(req, "500 Internal Server Error", "save_failed", "could not persist credentials");
        cJSON_Delete(root); return ESP_OK;
    }
    cJSON_Delete(root);

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_sendstr(req, "{\"ok\":true,\"message\":\"saved — rebooting to join your network\"}");

    // Reboot shortly so the new credentials take effect via the normal path.
    xTaskCreate(reboot_task, "carl_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

// ---------- static web dashboard (LittleFS) ----------

#define WEB_BASE_PATH "/littlefs"

// Mount the `littlefs` partition (flashed from data/ with the project image).
// Returns true on success; the static handler degrades to 404s if it fails.
static bool mount_web_fs(void) {
    esp_vfs_littlefs_conf_t conf = {
        .base_path              = WEB_BASE_PATH,
        .partition_label        = "littlefs",
        .dont_mount             = false,
        .format_if_mount_failed = false,  // image is flashed, never format
    };
    esp_err_t err = esp_vfs_littlefs_register(&conf);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "littlefs mount failed (%s) — dashboard unavailable",
                 esp_err_to_name(err));
        return false;
    }
    size_t total = 0, used = 0;
    esp_littlefs_info("littlefs", &total, &used);
    ESP_LOGI(TAG, "littlefs mounted: %u/%u bytes used", (unsigned)used, (unsigned)total);
    return true;
}

static const char *content_type_for(const char *path) {
    const char *dot = strrchr(path, '.');
    if (dot != NULL) {
        if (strcmp(dot, ".html") == 0) return "text/html; charset=utf-8";
        if (strcmp(dot, ".css")  == 0) return "text/css";
        if (strcmp(dot, ".js")   == 0) return "application/javascript";
        if (strcmp(dot, ".json") == 0) return "application/json";
        if (strcmp(dot, ".svg")  == 0) return "image/svg+xml";
        if (strcmp(dot, ".png")  == 0) return "image/png";
        if (strcmp(dot, ".ico")  == 0) return "image/x-icon";
        if (strcmp(dot, ".woff2")== 0) return "font/woff2";
    }
    return "text/plain";
}

// Catch-all GET: serve a file from the LittleFS web partition. "/" maps to
// index.html; an unknown path 404s. Registered LAST so the /api/* routes win.
static esp_err_t handle_static(httpd_req_t *req) {
    // Path portion only (strip any query string).
    char uripath[256];
    const char *q = strchr(req->uri, '?');
    size_t ulen = q ? (size_t)(q - req->uri) : strlen(req->uri);
    if (ulen >= sizeof(uripath)) { httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "uri too long"); return ESP_OK; }
    memcpy(uripath, req->uri, ulen);
    uripath[ulen] = '\0';

    // No directory traversal, and default the root to index.html.
    if (strstr(uripath, "..") != NULL) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
        return ESP_OK;
    }
    // Onboarding: every page request lands on the Wi-Fi setup form so the OS
    // captive-portal probe (e.g. /hotspot-detect.html, /generate_204) opens it.
    if (s_setup_mode) strcpy(uripath, "/setup.html");
    else if (strcmp(uripath, "/") == 0) strcpy(uripath, "/index.html");

    char path[300];
    snprintf(path, sizeof(path), WEB_BASE_PATH "%s", uripath);

    FILE *f = fopen(path, "r");
    if (f == NULL) {
        // SPA fallback: unknown non-asset routes get index.html so client-side
        // routing works; genuinely missing assets still 404 via the dot check.
        if (strchr(uripath, '.') == NULL) {
            f = fopen(WEB_BASE_PATH "/index.html", "r");
            if (f != NULL) strcpy(path, WEB_BASE_PATH "/index.html");
        }
        if (f == NULL) {
            httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "not found");
            return ESP_OK;
        }
    }

    httpd_resp_set_type(req, content_type_for(path));
    char buf[1024];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, n) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  // end of response
    return ESP_OK;
}

// ---------- CORS preflight ----------

static esp_err_t handle_cors_preflight(httpd_req_t *req) {
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "GET, POST, PATCH, DELETE, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_hdr(req, "Access-Control-Max-Age", "86400");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// ---------- server startup ----------

void carl_http_api_start(void) {
    mount_web_fs();  // best-effort; static handler 404s if unavailable

    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port      = CONFIG_CARL_HTTP_PORT;
    config.lru_purge_enable = true;
    config.uri_match_fn     = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/api/health",  .method = HTTP_GET,     .handler = handle_health },

        { .uri = "/api/nodes",   .method = HTTP_GET,     .handler = handle_nodes_get },
        { .uri = "/api/nodes",   .method = HTTP_POST,    .handler = handle_nodes_post },
        { .uri = "/api/nodes",   .method = HTTP_OPTIONS, .handler = handle_cors_preflight },

        // Wildcard handles both "/api/nodes/{id}" and "/api/nodes/{id}/history".
        { .uri = "/api/nodes/*", .method = HTTP_GET,     .handler = handle_node_get },
        { .uri = "/api/nodes/*", .method = HTTP_PATCH,   .handler = handle_node_patch },
        { .uri = "/api/nodes/*", .method = HTTP_DELETE,  .handler = handle_node_delete },
        { .uri = "/api/nodes/*", .method = HTTP_OPTIONS, .handler = handle_cors_preflight },

        { .uri = "/api/keys",    .method = HTTP_GET,     .handler = handle_keys_list },
        { .uri = "/api/keys",    .method = HTTP_POST,    .handler = handle_keys_post },
        { .uri = "/api/keys",    .method = HTTP_OPTIONS, .handler = handle_cors_preflight },
        { .uri = "/api/keys/*",  .method = HTTP_DELETE,  .handler = handle_keys_delete },
        { .uri = "/api/keys/*",  .method = HTTP_OPTIONS, .handler = handle_cors_preflight },

        { .uri = "/api/setup/wifi", .method = HTTP_POST,    .handler = handle_setup_wifi },
        { .uri = "/api/setup/wifi", .method = HTTP_OPTIONS, .handler = handle_cors_preflight },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }

    // Catch-all static handler for the web dashboard. MUST be registered last
    // so the specific /api/* routes above match first; everything else falls
    // through to a file lookup on the LittleFS partition.
    httpd_uri_t static_route = { .uri = "/*", .method = HTTP_GET, .handler = handle_static };
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &static_route));

    ESP_LOGI(TAG, "HTTP API listening on :%d", CONFIG_CARL_HTTP_PORT);
}
