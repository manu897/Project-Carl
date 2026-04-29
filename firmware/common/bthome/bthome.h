// BTHome v2 encoder for Project-Carl sensor nodes.
//
// Encodes a sequence of sensor measurements into an encrypted BTHome v2
// advertisement payload (service data, UUID 0xFCD2). Uses AES-CCM-128 with a
// 16-byte per-device key, 4-byte counter, 4-byte MIC.
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

// Maximum payload we need to assemble. BLE advertisement allows 31 bytes total;
// minus AD-structure headers we have ~24 bytes of service data. Sized to fit.
static constexpr size_t kMaxAdPayload = 31;

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

    // Encrypt with AES-CCM-128 and emit the BTHome v2 service-data body
    // (DeviceInfo byte + ciphertext + 4B counter + 4B MIC).
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
    uint8_t plaintext_[kMaxAdPayload];
    size_t  plaintext_len_;
    bool    error_;
};

// Hub-side decoder API lives in bthome_decode.h — separated so sensor-node
// firmware doesn't pull it in.

}  // namespace carl::bthome
