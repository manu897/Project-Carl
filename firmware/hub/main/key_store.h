// Per-node AES-CCM key store on the hub.
//
// V1 (Phase 3b) seeded a single test node from menuconfig. V2 (Phase 3c)
// persists keys to NVS and exposes add/remove/list so the HTTP API can
// provision nodes dynamically:
//   POST   /api/keys   — provision a node (MAC + AES key)
//   GET    /api/keys   — list provisioned MACs (keys not exposed)
//   DELETE /api/keys/* — revoke a node
//
// Menuconfig test-node entries are still loaded at init as a fallback so
// existing bring-up setups keep working. Once provisioned via the API,
// the NVS version takes priority.

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the store: load persisted keys from NVS, then add any
// menuconfig test-node entries that aren't already present. Idempotent.
void carl_key_store_init(void);

// Look up a 16-byte AES key by source MAC (little-endian, as NimBLE
// returns). Returns NULL if no key registered for this MAC.
const uint8_t *carl_key_store_lookup(const uint8_t mac6_le[6]);

// Number of registered keys (helpful for /api/health).
unsigned carl_key_store_count(void);

// Add a node key. Persists to NVS so it survives reboot.
// Returns true on success, false if the table is full or NVS write fails.
// If the MAC already exists, the key is silently updated.
bool carl_key_store_add(const uint8_t mac6_le[6], const uint8_t key[16]);

// Remove a node key by MAC. Erases from NVS.
// Returns true if found and removed, false if the MAC wasn't provisioned.
bool carl_key_store_remove(const uint8_t mac6_le[6]);

// Callback for iterating provisioned MACs. Return true to continue.
typedef bool (*carl_key_store_iter_fn)(const uint8_t mac6_le[6], void *user);

// Call fn once per provisioned node. Keys are NOT exposed — only MACs.
void carl_key_store_foreach(carl_key_store_iter_fn fn, void *user);

// --- Parsing helpers (shared with http_api.c) ---

// Parse "AA:BB:CC:DD:EE:FF" into little-endian 6 bytes. Returns true on success.
bool carl_key_store_parse_mac(const char *s, uint8_t out_le[6]);

// Parse 32 hex characters into 16 bytes. Returns true on success.
bool carl_key_store_parse_key(const char *s, uint8_t out[16]);

#ifdef __cplusplus
}
#endif
