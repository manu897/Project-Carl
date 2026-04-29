// Thin AES-CCM-128 wrapper backed by mbedtls.
//
// Both Arduino-ESP32 and Arduino-mbed (Nordic) ship mbedtls, so the same
// implementation file works for sensor nodes and the hub. A native build
// (host-side unit tests) needs mbedtls available on the system.
//
// All BTHome v2 traffic uses 4-byte MIC, so the helpers below hardcode that.

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace carl::aes_ccm {

static constexpr size_t kKeyLen   = 16;
static constexpr size_t kNonceLen = 13;
static constexpr size_t kMicLen   = 4;

// Encrypt `pt_len` bytes from `pt` into `ct`, writing the 4-byte MIC to `mic`.
// `ct` may alias `pt`. Returns true on success.
bool encrypt(const uint8_t key[kKeyLen],
             const uint8_t nonce[kNonceLen],
             const uint8_t* pt, size_t pt_len,
             uint8_t* ct,
             uint8_t mic[kMicLen]);

// Authenticate-and-decrypt `ct_len` bytes from `ct` into `pt`, verifying `mic`.
// `pt` may alias `ct`. Returns false if MIC check fails.
bool decrypt(const uint8_t key[kKeyLen],
             const uint8_t nonce[kNonceLen],
             const uint8_t* ct, size_t ct_len,
             uint8_t* pt,
             const uint8_t mic[kMicLen]);

}  // namespace carl::aes_ccm
