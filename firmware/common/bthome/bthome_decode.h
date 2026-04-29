// BTHome v2 decoder for the Carl hub firmware.
//
// Inverse of Builder: takes the encrypted advertisement service-data body,
// authenticates + decrypts with AES-CCM, and invokes a callback once per
// decoded measurement.
//
// Replay protection: the caller must compare the returned counter against
// the last-seen counter for that node and reject non-increasing values
// before trusting the measurements.

#pragma once

#include <stdint.h>
#include <stddef.h>

#include "bthome.h"

namespace carl::bthome {

struct Measurement {
    ObjectId id;
    union {
        uint8_t  u8;
        uint16_t u16;
        uint32_t u24;   // pressure / illuminance — high byte is zero
        int16_t  i16;
    } raw;

    // Convenience accessors that return the canonical SI-ish value.
    // Caller picks the right one based on id.
    float asTemperatureC() const  { return raw.i16 * 0.01f; }
    float asHumidityPct() const   { return raw.u16 * 0.01f; }
    float asPressureHpa() const   { return raw.u24 * 0.01f; }
    float asIlluminance() const   { return raw.u24 * 0.01f; }
    float asMoisturePct() const   { return raw.u16 * 0.01f; }
    uint8_t asBattery() const     { return raw.u8; }
    uint8_t asPacketId() const    { return raw.u8; }
};

enum class DecodeStatus : uint8_t {
    kOk,
    kBadHeader,        // service UUID / device-info byte wrong
    kTruncated,        // payload too short for header + counter + MIC
    kAuthFailed,       // AES-CCM MIC check failed (wrong key or tampered)
    kUnknownObject,    // encountered an object ID we don't decode
    kBufferTooSmall,
};

// Callback invoked once per decoded object, in advertisement order.
// Returning false aborts further iteration.
using ObjectVisitor = bool (*)(const Measurement& m, void* user);

struct DecodedHeader {
    uint32_t counter;  // From the advertisement; caller enforces monotonicity.
};

// Decode an encrypted BTHome v2 service-data body.
//
//   in / in_len : the service-data bytes after the UUID (i.e. starting with the
//                 DeviceInfo byte).
//   key16       : the AES key associated with the source MAC.
//   mac6        : source MAC, little-endian.
//   visitor     : called once per object on success.
//   user        : opaque, passed back to visitor.
//   out_header  : if non-null, populated with counter on success.
DecodeStatus decodeEncrypted(const uint8_t* in,
                             size_t in_len,
                             const uint8_t key16[16],
                             const uint8_t mac6[6],
                             ObjectVisitor visitor,
                             void* user,
                             DecodedHeader* out_header);

}  // namespace carl::bthome
