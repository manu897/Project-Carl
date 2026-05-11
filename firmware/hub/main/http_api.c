#include "http_api.h"

#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "sdkconfig.h"

#include "node_registry.h"

static const char *TAG = "carl-api";

// Helpful for browser-side debugging from carl-hub.local — the iOS app +
// M5Paper reader speak server-to-server and don't need it, but it costs
// nothing to be CORS-friendly.
static void send_json(httpd_req_t *req, const char *body, size_t len) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_send(req, body, len);
}

// GET /api/health — uptime, build version, free heap. Used by readers as a
// liveness probe and for surfacing build info on the dashboard.
static esp_err_t handle_health(httpd_req_t *req) {
    const esp_app_desc_t *app = esp_app_get_description();
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", "carl-hub");
    cJSON_AddStringToObject(root, "version",
                            (app != NULL) ? app->version : "unknown");
    cJSON_AddNumberToObject(root, "uptime_ms",
                            (double)(esp_timer_get_time() / 1000));
    cJSON_AddNumberToObject(root, "free_heap_bytes",
                            (double)esp_get_free_heap_size());
    cJSON_AddNumberToObject(root, "min_free_heap_bytes",
                            (double)esp_get_minimum_free_heap_size());

    char *body = cJSON_PrintUnformatted(root);
    send_json(req, body, strlen(body));
    free(body);
    cJSON_Delete(root);
    return ESP_OK;
}

// GET /api/nodes — list of plants with latest readings. Phase 3a returns
// an empty array; Phase 3b populates from the registry.
static esp_err_t handle_nodes_list(httpd_req_t *req) {
    char *body = carl_node_registry_render_json();
    if (body == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    send_json(req, body, strlen(body));
    free(body);
    return ESP_OK;
}

void carl_http_api_start(void) {
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port    = CONFIG_CARL_HTTP_PORT;
    config.lru_purge_enable = true;
    config.uri_match_fn   = httpd_uri_match_wildcard;

    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd_start failed");
        return;
    }

    httpd_uri_t routes[] = {
        { .uri = "/api/health", .method = HTTP_GET, .handler = handle_health },
        { .uri = "/api/nodes",  .method = HTTP_GET, .handler = handle_nodes_list },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); ++i) {
        ESP_ERROR_CHECK(httpd_register_uri_handler(server, &routes[i]));
    }
    ESP_LOGI(TAG, "HTTP API listening on :%d", CONFIG_CARL_HTTP_PORT);
}
