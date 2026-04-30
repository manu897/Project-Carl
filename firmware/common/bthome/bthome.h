// BTHome v2 encoder for Project-Carl sensor nodes.
//
// Encodes a sequence of sensor measurements into an encrypted BTHome v2
// service-data value: [UUID16 LE][DeviceInfo][ciphertext][counter LE][MIC].
// Drop the result directly into the AD structure of type 0x16 (Service Data,
// 16-bit UUID). Uses AES-CCM-128 with a 16-byte per-device key, 4-byte
// counter, 4-byte MIC.
//
// Reference: https://bthome.io/format/
//
// Usage on a sensor node:
//
//   carl::bthome::Builder b;
//   b.addPacketId(seq);
//   b.addTemperature(21.34f);
//   b.addHumidity(48.0f);
//   b.addPressure(1013.25f);
//   b.addMoisture(32.5f);
//   b.addIlluminance(120.0f);
//   b.addBattery(87);
//
//   uint8_t out[BTHOME_MAX_AD_PAYLOAD];
//   size_t out_len = 0;
//   if (b.buildEncrypted(out, sizeof(out), &out_len, key16, mac6, counter)) {
//       // out / out_len are ready to set as the advertisement service data.
//   }

#pragma once

#include <stdint.h>
#include <stddef.h>

namespace carl::bthome {

// 16-bit UUID for BTHome v2 service data (LE on the wire).
static constexpr uint16_t kServiceUuid = 0xFCD2;

// Device-Information byte for an encrypted BTHome v2 frame:
//   bit 0       : encryption flag (1)
//   bits 5..7   : version (010 = v2)
static constexpr uint8_t kDeviceInfoEncryptedV2 = 0x41;

// Maximum plaintext (sensor objects, before AES-CCM) that the Builder accepts.
// 64 bytes leaves headroom for any object combination we might encode.
//
// On-air practicality:
//   * Encrypted output = plaintext + 11 bytes (UUID + DeviceInfo + counter + MIC).
//   * Legacy BLE non-connectable advertising allows 31 bytes total, of which
//     ~26 are usable for one Service Data AD struct after Flags AD overhead;
//     subtracting the 11-byte BTHome overhead leaves ~15 bytes of plaintext.
//   * Nodes that want more than ~15 bytes of plaintext per cycle should either
//     use BLE 5 extended advertising or split readings across alternating
//     advertisements (e.g. T/H/P one cycle, moisture/lux the next).
//
// The encoder does not enforce the legacy-BLE limit — callers do.
static constexpr size_t kMaxPlaintext = 64;

// Standard BTHome object IDs we use.
//
// Object id     | Type            | Resolution
//---------------+-----------------+-----------
// kPacketId     | uint8           | counter (rolls; helps HA dedup)
// kBattery      | uint8 [%]       | 1
// kTemperature  | int16 [°C]      | 0.01
// kHumidity     | uint16 [%]      | 0.01
// kPressure     | uint24 [hPa]    | 0.01
// kIlluminance  | uint24 [lux]    | 0.01
// kMoisture     | uint16 [%]      | 0.01  (HA renders as "Soil Moisture")
enum ObjectId : uint8_t {
    kPacketId    = 0x00,
    kBattery     = 0x01,
    kTemperature = 0x02,
    kHumidity    = 0x03,
    kPressure    = 0x04,
    kIlluminance = 0x05,
    kMoisture    = 0x3F,
};

// Builder accumulates objects in a fixed-size buffer, then emits an encrypted
// advertisement payload on demand. Stack-allocatable; no dynamic memory.
class Builder {
public:
    Builder();

    // Append measurements. Each returns false if the working buffer would
    // overflow; on first failure the builder enters an error state and any
    // further build() returns false.
    bool addPacketId(uint8_t id);
    bool addBattery(uint8_t percent);
    bool addTemperature(float celsius);     // -327.68 .. +327.67 °C
    bool addHumidity(float percent);        // 0 .. 100 %
    bool addPressure(float hpa);            // 0 .. ~1677 hPa
    bool addIlluminance(float lux);         // 0 .. ~167772 lux
    bool addMoisture(float percent);        // 0 .. 100 % (soil)

    // Encrypt with AES-CCM-128 and emit the full BTHome v2 service-data value:
    //   [UUID16 LE: 2B] [DeviceInfo: 1B] [ciphertext] [counter LE: 4B] [MIC: 4B]
    //
    //   key16   : 16-byte AES key, persisted on the node
    //   mac6    : node's BLE MAC address, little-endian (6 bytes)
    //   counter : monotonically increasing; persist across reboots in NVS
    //
    // Writes at most out_capacity bytes; on success sets *out_len.
    // Returns false if the builder is in error state, output too small, or
    // the underlying AES operation fails.
    bool buildEncrypted(uint8_t* out,
                        size_t out_capacity,
                        size_t* out_len,
                        const uint8_t key16[16],
                        const uint8_t mac6[6],
                        uint32_t counter) const;

    // For unit tests / debugging: return the unencrypted body that
    // buildEncrypted() would feed to AES-CCM.
    size_t plaintextSize() const { return plaintext_len_; }
    const uint8_t* plaintext() const { return plaintext_; }
    bool ok() const { return !error_; }

private:
    bool append(const uint8_t* bytes, size_t n);

    // Unencrypted BTHome objects, before AES-CCM. Never larger than the AD payload.
    uint8_t plaintext_[kMaxPlaintext];
    size_t  plaintext_len_;
    bool    error_;
};

// Hub-side decoder API lives in bthome_decode.h — separated so sensor-node
// firmware doesn't pull it in.

}  // namespace carl::bthome
