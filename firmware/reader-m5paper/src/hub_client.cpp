#include "hub_client.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include <cstring>

namespace carl::hub {

namespace {

char g_base_url[64] = "http://carl-hub.local";

void copyTrunc(char* dst, size_t cap, const char* src) {
    if (src == nullptr) { dst[0] = '\0'; return; }
    std::strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void readReading(JsonObjectConst src, Reading* r) {
    *r = Reading{};
    if (!src["temperature_c"].isNull()) {
        r->has_temperature_c = true;
        r->temperature_c = src["temperature_c"].as<float>();
    }
    if (!src["humidity_pct"].isNull()) {
        r->has_humidity_pct = true;
        r->humidity_pct = src["humidity_pct"].as<float>();
    }
    if (!src["pressure_hpa"].isNull()) {
        r->has_pressure_hpa = true;
        r->pressure_hpa = src["pressure_hpa"].as<float>();
    }
    if (!src["soil_pct"].isNull()) {
        r->has_soil_pct = true;
        r->soil_pct = src["soil_pct"].as<float>();
    }
    if (!src["illuminance_lux"].isNull()) {
        r->has_illuminance = true;
        r->illuminance_lux = src["illuminance_lux"].as<float>();
    }
    if (!src["battery_pct"].isNull()) {
        r->has_battery_pct = true;
        r->battery_pct = src["battery_pct"].as<uint8_t>();
    }
    copyTrunc(r->ts_iso, sizeof(r->ts_iso), src["ts"].as<const char*>());
}

bool decodeNodesArray(JsonArrayConst arr, NodeList* out) {
    out->count = 0;
    for (JsonObjectConst entry : arr) {
        if (out->count >= NodeList::kMaxNodes) break;
        Node& n = out->nodes[out->count++];
        n = Node{};
        copyTrunc(n.id,   sizeof(n.id),   entry["id"].as<const char*>());
        copyTrunc(n.name, sizeof(n.name), entry["name"].as<const char*>());
        copyTrunc(n.mac,  sizeof(n.mac),  entry["mac"].as<const char*>());
        n.online = entry["online"].as<bool>();
        copyTrunc(n.last_seen_iso, sizeof(n.last_seen_iso),
                  entry["last_seen"].as<const char*>());
        if (!entry["battery_pct"].isNull()) {
            n.has_battery_pct = true;
            n.battery_pct = entry["battery_pct"].as<uint8_t>();
        }
        readReading(entry["latest"].as<JsonObjectConst>(), &n.latest);
    }
    out->fetch_ok = true;
    out->err_msg[0] = '\0';
    return true;
}

bool decodeHistoryArray(JsonArrayConst arr, History* out) {
    out->count = 0;
    for (JsonObjectConst entry : arr) {
        if (out->count >= History::kMaxSamples) break;
        readReading(entry, &out->samples[out->count++]);
    }
    out->fetch_ok = true;
    out->err_msg[0] = '\0';
    return true;
}

#ifndef CARL_READER_USE_MOCK
// Resolve g_base_url + a path/query suffix into a request-ready URL,
// working around Arduino-ESP32 HTTPClient not resolving .local hostnames
// via mDNS itself (it hands them straight to gethostbyname(), which most
// home routers don't relay). Shared by fetchNodes() and fetchHistory() so
// the resolution logic lives in exactly one place.
bool resolveUrl(const char* path_and_query, char* out_url, size_t cap,
                char* err_msg, size_t err_cap) {
    if (std::strstr(g_base_url, ".local") == nullptr) {
        std::snprintf(out_url, cap, "%s%s", g_base_url, path_and_query);
        return true;
    }
    // Pull "carl-hub" out of "http://carl-hub.local"
    const char *host_start = std::strstr(g_base_url, "//");
    host_start = (host_start == nullptr) ? g_base_url : host_start + 2;
    const char *host_end = std::strchr(host_start, '.');
    if (host_end == nullptr) host_end = host_start + std::strlen(host_start);
    char hostname[40];
    const size_t n = host_end - host_start;
    if (n >= sizeof(hostname)) {
        std::strncpy(err_msg, "hostname too long", err_cap);
        return false;
    }
    std::memcpy(hostname, host_start, n);
    hostname[n] = '\0';

    IPAddress ip = MDNS.queryHost(hostname, 3000);
    if (ip == INADDR_NONE || ip == IPAddress(0, 0, 0, 0)) {
        std::snprintf(err_msg, err_cap, "mdns: %s.local unreachable", hostname);
        return false;
    }
    std::snprintf(out_url, cap, "http://%s%s", ip.toString().c_str(), path_and_query);
    return true;
}
#endif

#ifdef CARL_READER_USE_MOCK
constexpr const char* kMockJson = R"JSON([
  {
    "id": "node-1",
    "mac": "AA:BB:CC:DD:EE:01",
    "name": "Bedroom Monstera",
    "last_seen": "2026-05-04T14:32:00Z",
    "online": true,
    "battery_pct": 87,
    "latest": {
      "ts": "2026-05-04T14:32:00Z",
      "temperature_c": 22.4,
      "humidity_pct": 47.2,
      "pressure_hpa": 1013.1,
      "soil_pct": 38.0,
      "illuminance_lux": 340,
      "battery_pct": 87
    }
  },
  {
    "id": "node-2",
    "mac": "AA:BB:CC:DD:EE:02",
    "name": "Kitchen Basil",
    "last_seen": "2026-05-04T14:31:00Z",
    "online": true,
    "battery_pct": 62,
    "latest": {
      "ts": "2026-05-04T14:31:00Z",
      "temperature_c": 23.1,
      "humidity_pct": 51.0,
      "pressure_hpa": 1013.1,
      "soil_pct": 12.0,
      "illuminance_lux": 880,
      "battery_pct": 62
    }
  }
])JSON";

// A plausible 24h soil curve for node-1: watered around 06:00 (soil near
// 60%), drying out through the day down toward 20% by evening — enough
// shape to sanity-check the detail-page graph without a hub attached.
constexpr const char* kMockHistoryJson = R"JSON({
  "node_id": "node-1",
  "range": "24h",
  "samples": [
    {"ts": "2026-05-04T06:00:00Z", "soil_pct": 61.0, "temperature_c": 20.1, "humidity_pct": 44.0},
    {"ts": "2026-05-04T08:00:00Z", "soil_pct": 55.0, "temperature_c": 20.8, "humidity_pct": 43.0},
    {"ts": "2026-05-04T10:00:00Z", "soil_pct": 48.0, "temperature_c": 21.6, "humidity_pct": 42.0},
    {"ts": "2026-05-04T12:00:00Z", "soil_pct": 41.0, "temperature_c": 22.5, "humidity_pct": 41.0},
    {"ts": "2026-05-04T14:00:00Z", "soil_pct": 38.0, "temperature_c": 22.4, "humidity_pct": 47.2},
    {"ts": "2026-05-04T16:00:00Z", "soil_pct": 32.0, "temperature_c": 22.0, "humidity_pct": 45.0},
    {"ts": "2026-05-04T18:00:00Z", "soil_pct": 27.0, "temperature_c": 21.5, "humidity_pct": 44.0},
    {"ts": "2026-05-04T20:00:00Z", "soil_pct": 23.0, "temperature_c": 20.9, "humidity_pct": 44.0},
    {"ts": "2026-05-04T22:00:00Z", "soil_pct": 20.0, "temperature_c": 20.3, "humidity_pct": 45.0}
  ]
})JSON";
#endif

}  // namespace

void setBaseUrl(const char* url) {
    copyTrunc(g_base_url, sizeof(g_base_url), url);
}

bool fetchNodes(NodeList* out) {
    JsonDocument doc;
#ifdef CARL_READER_USE_MOCK
    auto err = deserializeJson(doc, kMockJson);
    if (err) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg),
                      "mock parse: %s", err.c_str());
        return false;
    }
    return decodeNodesArray(doc.as<JsonArrayConst>(), out);
#else
    if (WiFi.status() != WL_CONNECTED) {
        out->fetch_ok = false;
        std::strncpy(out->err_msg, "wifi down", sizeof(out->err_msg));
        return false;
    }

    char url[128];
    if (!resolveUrl("/api/nodes", url, sizeof(url), out->err_msg, sizeof(out->err_msg))) {
        out->fetch_ok = false;
        return false;
    }

    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(url)) {
        out->fetch_ok = false;
        std::strncpy(out->err_msg, "http begin failed", sizeof(out->err_msg));
        return false;
    }
    const int code = http.GET();
    if (code != 200) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg), "http %d", code);
        http.end();
        return false;
    }
    auto err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg),
                      "json: %s", err.c_str());
        return false;
    }
    return decodeNodesArray(doc.as<JsonArrayConst>(), out);
#endif
}

bool fetchHistory(const char* node_id, const char* range, History* out) {
    JsonDocument doc;
#ifdef CARL_READER_USE_MOCK
    (void)node_id; (void)range;
    auto err = deserializeJson(doc, kMockHistoryJson);
    if (err) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg),
                      "mock parse: %s", err.c_str());
        return false;
    }
    return decodeHistoryArray(doc["samples"].as<JsonArrayConst>(), out);
#else
    if (WiFi.status() != WL_CONNECTED) {
        out->fetch_ok = false;
        std::strncpy(out->err_msg, "wifi down", sizeof(out->err_msg));
        return false;
    }

    char path[80];
    std::snprintf(path, sizeof(path), "/api/nodes/%s/history?range=%s", node_id, range);
    char url[160];
    if (!resolveUrl(path, url, sizeof(url), out->err_msg, sizeof(out->err_msg))) {
        out->fetch_ok = false;
        return false;
    }

    HTTPClient http;
    http.setTimeout(5000);
    if (!http.begin(url)) {
        out->fetch_ok = false;
        std::strncpy(out->err_msg, "http begin failed", sizeof(out->err_msg));
        return false;
    }
    const int code = http.GET();
    if (code != 200) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg), "http %d", code);
        http.end();
        return false;
    }
    auto err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) {
        out->fetch_ok = false;
        std::snprintf(out->err_msg, sizeof(out->err_msg),
                      "json: %s", err.c_str());
        return false;
    }
    return decodeHistoryArray(doc["samples"].as<JsonArrayConst>(), out);
#endif
}

}  // namespace carl::hub
