// Carl-hub HTTP client used by the M5Paper reader.
//
// Polls `http://carl-hub.local/api/nodes` and parses the JSON shape defined
// by Project-Carl-IOS/api/openapi.yaml — the same contract the iOS app
// codegens against. The hub is the source of truth: it owns BLE scanning,
// AES key storage, and history. The reader just renders.
//
// Built-in mock mode (CARL_READER_USE_MOCK=1) returns a hardcoded list of
// nodes from a baked-in JSON literal so the firmware works against real
// hardware before Phase 3 hub firmware exists. Same API surface either way.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace carl::hub {

struct Reading {
    bool    has_temperature_c;  float temperature_c;
    bool    has_humidity_pct;   float humidity_pct;
    bool    has_pressure_hpa;   float pressure_hpa;
    bool    has_soil_pct;       float soil_pct;
    bool    has_illuminance;    float illuminance_lux;
    bool    has_battery_pct;    uint8_t battery_pct;
    char    ts_iso[32];          // ISO-8601 timestamp from the hub
};

struct Node {
    char    id[32];               // hub-assigned node id
    char    name[40];             // friendly name (eg "Bedroom Monstera")
    char    mac[20];              // canonical "AA:BB:CC:DD:EE:FF"
    bool    online;
    char    last_seen_iso[32];
    bool    has_battery_pct; uint8_t battery_pct;
    Reading latest;
};

// Result of one polling cycle.
struct NodeList {
    static constexpr size_t kMaxNodes = 16;
    Node    nodes[kMaxNodes];
    size_t  count;
    bool    fetch_ok;             // false = polling error, last good list intact
    char    err_msg[64];
};

// Result of a GET /api/nodes/{id}/history?range=... call. Samples arrive
// oldest-first, matching the hub's node_registry.c ring-buffer order.
// kMaxSamples matches the hub's HIST_CAP (48) so a full history response
// decodes without truncation.
struct History {
    static constexpr size_t kMaxSamples = 48;
    Reading samples[kMaxSamples];
    size_t  count;
    bool    fetch_ok;
    char    err_msg[64];
};

// Set the hub base URL. Default is "http://carl-hub.local". Override if
// the user's mDNS is flaky and they configured an IP via the touch UI.
void setBaseUrl(const char* url);

// Synchronously fetch /api/nodes and decode into `out`. Returns true on
// success. Real network call when CARL_READER_USE_MOCK is unset; canned
// data otherwise.
bool fetchNodes(NodeList* out);

// Synchronously fetch /api/nodes/{node_id}/history?range={range} (range is
// "24h", "7d", or "30d" — matches the hub's OpenAPI contract) and decode
// into `out`. Returns true on success. Mocked with a plausible declining/
// recovering soil curve when CARL_READER_USE_MOCK is set, so the detail
// page + graph can be bench-tested with no hub attached.
bool fetchHistory(const char* node_id, const char* range, History* out);

}  // namespace carl::hub
