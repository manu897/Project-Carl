// Per-node AES-CCM key store on the hub.
//
// V1 (Phase 3b): seeds a single test node from menuconfig values
// (CARL_TEST_NODE_MAC + CARL_TEST_NODE_KEY) so we can validate the BLE
// decrypt path end-to-end against one real sensor. Later phases:
//   3c — POST /api/keys to provision dynamically, NVS-backed
//   3d — DELETE /api/keys/<mac> for revocation

#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the store: parse the Kconfig test-node MAC + key. Idempotent.
void carl_key_store_init(void);

// Look up a 16-byte AES key by source MAC (little-endian, as NimBLE
// returns). Returns NULL if no key registered for this MAC.
const uint8_t *carl_key_store_lookup(const uint8_t mac6_le[6]);

// Number of registered keys (helpful for /api/health).
unsigned carl_key_store_count(void);

#ifdef __cplusplus
}
#endif
