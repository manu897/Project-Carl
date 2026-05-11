// In-memory registry of every plant node the hub has heard from.
//
// The BLE scanner pushes decoded readings here via carl_node_registry_update()
// and the HTTP server pulls a JSON snapshot via carl_node_registry_render_json()
// on every /api/nodes hit. Schema matches Project-Carl-IOS/api/openapi.yaml's
// Node[] type so iOS app + M5Paper reader compile against one contract.

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

void carl_node_registry_init(void);

// Called by the BLE scanner whenever a fresh advertisement decodes cleanly.
// `mac6_le` is little-endian (NimBLE convention). `counter` is the BTHome
// 4-byte replay counter from the encrypted frame; older counters are
// ignored to defend against replay attacks.
void carl_node_registry_update(const uint8_t mac6_le[6],
                               uint32_t counter,
                               const carl_reading_t *reading);

// Allocate + return a JSON Node[] string (caller frees). Always returns
// at least "[]" — never NULL on success, never partial JSON.
char *carl_node_registry_render_json(void);

#ifdef __cplusplus
}
#endif
