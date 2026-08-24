// In-memory registry of every node the hub has heard from.
//
// The BLE scanner pushes decoded readings here via carl_node_registry_update()
// and the HTTP server pulls JSON snapshots out. Schema matches
// Project-Carl-IOS/api/openapi.yaml so the iOS app + M5Paper reader compile
// against one contract.
//
// Phase 3d additions:
//   - per-node name + calibration thresholds (NVS-persisted)
//   - a recent-history ring buffer per node (RAM; covers ~24h at the normal
//     30-min cadence — longer ranges return what's retained, with full
//     long-term history owned by Project-Norman via the daily upload)
//   - single-node / history / metadata-update / remove operations backing
//     the /api/nodes/{id} family

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool     temp_ok;       float temp_c;
    bool     humidity_ok;   float humidity_pct;
    bool     pressure_ok;   float pressure_hpa;
    bool     soil_ok;       float soil_pct;
    bool     lux_ok;        float lux;
    bool     battery_ok;    uint8_t battery_pct;
    uint8_t  packet_id;
} carl_reading_t;

// Hub-side per-node thresholds (the OpenAPI Calibration schema). These are
// hub/app alerting knobs, NOT raw soil ADC calibration — the broadcast-only
// node can't receive pushed cal, so it self-calibrates via its button. The
// hub uses these to classify soil %, flag low battery, and decide online.
typedef struct {
    float    soil_dry_pct;        // default 25.0
    float    soil_wet_pct;        // default 65.0
    uint8_t  battery_low_pct;     // default 15
    uint16_t offline_after_min;   // default 30
} carl_calibration_t;

void carl_node_registry_init(void);

// Called by the BLE scanner whenever a fresh advertisement decodes cleanly.
// `mac6_le` is little-endian (NimBLE convention). `counter` is the BTHome
// 4-byte replay counter; older counters are ignored (replay defence).
void carl_node_registry_update(const uint8_t mac6_le[6],
                               uint32_t counter,
                               const carl_reading_t *reading);

// Number of nodes the hub has heard from (for /api/health node_count).
unsigned carl_node_registry_count(void);

// Read-only snapshot of one node, handed to a visitor callback. The string
// pointers are valid only for the duration of the callback.
typedef struct {
    const char           *id;     // "node-AABB"
    const char           *name;
    const char           *mac;    // "AA:BB:CC:DD:EE:FF"
    bool                  online;
    carl_reading_t        latest; // merged latest reading
} carl_node_snapshot_t;

typedef void (*carl_node_visit_fn)(const carl_node_snapshot_t *snap, void *user);

// Invoke `fn` once per known node (under the registry lock). Used by the MQTT
// bridge (Home Assistant) and the Norman uplink to publish current readings.
void carl_node_registry_foreach(carl_node_visit_fn fn, void *user);

// --- JSON renderers (caller frees the returned string) ---

// Full Node[] array. Always returns at least "[]" — never NULL.
char *carl_node_registry_render_json(void);

// One Node object by id (e.g. "node-3936"). Returns NULL if no such node.
char *carl_node_registry_get_json(const char *id);

// History object for a node + range ("24h" | "7d" | "30d"). Returns NULL if
// no such node. Unknown ranges fall back to "24h".
char *carl_node_registry_history_json(const char *id, const char *range);

// --- Mutations backing PATCH / DELETE / POST ---

// Update a heard node's metadata by id (PATCH /api/nodes/{id}). Any of name /
// cal / node_type / room_id may be NULL to leave that field unchanged.
// node_type is "plant" or "room"; room_id links plants to their room node.
// Persists to NVS. Returns false if no node with that id is known yet.
bool carl_node_registry_update_meta(const char *id,
                                     const char *name,
                                     const carl_calibration_t *cal,
                                     const char *node_type,
                                     const char *room_id);

// Stash metadata for a MAC that may not have been heard yet (POST /api/nodes
// provisions before the first ad arrives). Persists to NVS keyed by MAC;
// applied to the registry entry when the node is first heard. Pass NULL for
// any field to keep the existing/default value.
void carl_node_registry_set_meta_by_mac(const uint8_t mac6_le[6],
                                         const char *name,
                                         const carl_calibration_t *cal,
                                         const char *node_type,
                                         const char *room_id);

// Remove a node by id (DELETE /api/nodes/{id}). On success writes the node's
// little-endian MAC to out_mac_le (so the caller can purge its AES key) and
// returns true. Returns false if no such node.
bool carl_node_registry_remove(const char *id, uint8_t out_mac_le[6]);

#ifdef __cplusplus
}
#endif
