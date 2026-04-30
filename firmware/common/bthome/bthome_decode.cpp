#include "bthome_decode.h"

#include <cstring>

#include "aes_ccm.h"

namespace carl::bthome {

namespace {

constexpr size_t kHeaderLen  = 3;  // UUID16 + DeviceInfo
constexpr size_t kCounterLen = 4;
constexpr size_t kFooterLen  = kCounterLen + carl::aes_ccm::kMicLen;
constexpr size_t kMinFrame   = kHeaderLen + kFooterLen;  // 11 bytes

uint32_t readLE32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16)
         | (static_cast<uint32_t>(p[3]) << 24);
}

uint16_t readLE16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0])
         | (static_cast<uint16_t>(p[1]) << 8);
}

uint32_t readLE24(const uint8_t* p) {
    return static_cast<uint32_t>(p[0])
         | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16);
}

// Returns the wire size (object id byte + payload) of a known object id, or 0
// if the id is unrecognized.
size_t objectWireSize(ObjectId id) {
    switch (id) {
        case kPacketId:
        case kBattery:      return 1 + 1;
        case kTemperature:
        case kHumidity:
        case kMoisture:     return 1 + 2;
        case kPressure:
        case kIlluminance:  return 1 + 3;
    }
    return 0;
}

}  // namespace

DecodeStatus decodeEncrypted(const uint8_t* in,
                             size_t in_len,
                             const uint8_t key16[16],
                             const uint8_t mac6[6],
                             ObjectVisitor visitor,
                             void* user,
                             DecodedHeader* out_header) {
    if (in == nullptr || in_len < kMinFrame) return DecodeStatus::kTruncated;

    if (in[0] != 0xD2 || in[1] != 0xFC) return DecodeStatus::kBadHeader;
    if (in[2] != kDeviceInfoEncryptedV2) return DecodeStatus::kBadHeader;

    const size_t ct_len = in_len - kMinFrame;
    if (ct_len > kMaxPlaintext) return DecodeStatus::kBufferTooSmall;

    const uint8_t* ct       = in + kHeaderLen;
    const uint8_t* counterp = in + kHeaderLen + ct_len;
    const uint8_t* mic      = counterp + kCounterLen;
    const uint32_t counter  = readLE32(counterp);

    uint8_t nonce[carl::aes_ccm::kNonceLen];
    std::memcpy(nonce, mac6, 6);
    nonce[6] = 0xD2;
    nonce[7] = 0xFC;
    nonce[8] = kDeviceInfoEncryptedV2;
    nonce[9]  = static_cast<uint8_t>(counter & 0xFF);
    nonce[10] = static_cast<uint8_t>((counter >> 8) & 0xFF);
    nonce[11] = static_cast<uint8_t>((counter >> 16) & 0xFF);
    nonce[12] = static_cast<uint8_t>((counter >> 24) & 0xFF);

    uint8_t pt[kMaxPlaintext];
    if (!carl::aes_ccm::decrypt(key16, nonce, ct, ct_len, pt, mic)) {
        return DecodeStatus::kAuthFailed;
    }

    if (out_header != nullptr) out_header->counter = counter;

    size_t i = 0;
    while (i < ct_len) {
        const ObjectId id = static_cast<ObjectId>(pt[i]);
        const size_t wire = objectWireSize(id);
        if (wire == 0) return DecodeStatus::kUnknownObject;
        if (i + wire > ct_len) return DecodeStatus::kTruncated;

        Measurement m{};
        m.id = id;
        const uint8_t* p = pt + i + 1;
        switch (id) {
            case kPacketId:
            case kBattery:      m.raw.u8  = p[0]; break;
            case kTemperature:  m.raw.i16 = static_cast<int16_t>(readLE16(p)); break;
            case kHumidity:
            case kMoisture:     m.raw.u16 = readLE16(p); break;
            case kPressure:
            case kIlluminance:  m.raw.u24 = readLE24(p); break;
        }
        i += wire;

        if (visitor != nullptr && !visitor(m, user)) break;
    }
    return DecodeStatus::kOk;
}

}  // namespace carl::bthome
