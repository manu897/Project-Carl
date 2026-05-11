// BLE central scanner for the Carl hub.
//
// Initialises the NimBLE host, starts a passive continuous scan filtered
// for the BTHome v2 service UUID (0xFCD2), decrypts each matching
// advertisement using the per-node AES key from the key store, and pushes
// the decoded readings into the node registry.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void carl_ble_scanner_start(void);

#ifdef __cplusplus
}
#endif
