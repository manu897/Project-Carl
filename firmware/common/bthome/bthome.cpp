#include "bthome.h"

#include <cstring>

#include "aes_ccm.h"

namespace carl::bthome {

namespace {

// Fixed-point round-to-nearest helper for floats with a 0.01 quantum.
// Saturates to the [lo, hi] range. Returns int32 so callers can cast to the
// concrete object size.
int32_t toCenti(float v, int32_t lo, int32_t hi) {
    if (v >= 0.0f) v += 0.005f; else v -= 0.005f;
    int32_t q = static_cast<int32_t>(v * 100.0f);
    if (q < lo) q = lo;
    if (q > hi) q = hi;
    return q;
}

void writeLE16(uint8_t* dst, uint16_t v) {
    dst[0] = static_cast<uint8_t>(v & 0xFF);
    dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
}

void writeLE24(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>(v & 0xFF);
    dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
}

void writeLE32(uint8_t* dst, uint32_t v) {
    dst[0] = static_cast<uint8_t>(v & 0xFF);
    dst[1] = static_cast<uint8_t>((v >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((v >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((v >> 24) & 0xFF);
}

}  // namespace

Builder::Builder() : plaintext_len_(0), error_(false) {}

bool Builder::append(const uint8_t* bytes, size_t n) {
    if (error_) return false;
    if (plaintext_len_ + n > sizeof(plaintext_)) {
        error_ = true;
        return false;
    }
    std::memcpy(plaintext_ + plaintext_len_, bytes, n);
    plaintext_len_ += n;
    return true;
}

bool Builder::addPacketId(uint8_t id) {
    const uint8_t buf[2] = { kPacketId, id };
    return append(buf, sizeof(buf));
}

bool Builder::addBattery(uint8_t percent) {
    const uint8_t buf[2] = { kBattery, percent };
    return append(buf, sizeof(buf));
}

bool Builder::addTemperature(float celsius) {
    const int16_t v = static_cast<int16_t>(toCenti(celsius, -32768, 32767));
    uint8_t buf[3] = { kTemperature, 0, 0 };
    writeLE16(buf + 1, static_cast<uint16_t>(v));
    return append(buf, sizeof(buf));
}

bool Builder::addHumidity(float percent) {
    const uint16_t v = static_cast<uint16_t>(toCenti(percent, 0, 0xFFFF));
    uint8_t buf[3] = { kHumidity, 0, 0 };
    writeLE16(buf + 1, v);
    return append(buf, sizeof(buf));
}

bool Builder::addPressure(float hpa) {
    // Pressure is uint24 with 0.01 hPa resolution → max ~167772 hPa.
    const uint32_t v = static_cast<uint32_t>(toCenti(hpa, 0, 0xFFFFFF));
    uint8_t buf[4] = { kPressure, 0, 0, 0 };
    writeLE24(buf + 1, v);
    return append(buf, sizeof(buf));
}

bool Builder::addIlluminance(float lux) {
    const uint32_t v = static_cast<uint32_t>(toCenti(lux, 0, 0xFFFFFF));
    uint8_t buf[4] = { kIlluminance, 0, 0, 0 };
    writeLE24(buf + 1, v);
    return append(buf, sizeof(buf));
}

bool Builder::addMoisture(float percent) {
    const uint16_t v = static_cast<uint16_t>(toCenti(percent, 0, 0xFFFF));
    uint8_t buf[3] = { kMoisture, 0, 0 };
    writeLE16(buf + 1, v);
    return append(buf, sizeof(buf));
}

bool Builder::buildEncrypted(uint8_t* out,
                             size_t out_capacity,
                             size_t* out_len,
                             const uint8_t key16[16],
                             const uint8_t mac6[6],
                             uint32_t counter) const {
    if (error_ || out == nullptr || out_len == nullptr) return false;

    // Output: 2B UUID + 1B DeviceInfo + plaintext_len_ + 4B counter + 4B MIC.
    const size_t needed = 2 + 1 + plaintext_len_ + 4 + 4;
    if (out_capacity < needed) return false;

    out[0] = 0xD2;  // UUID16 LSB (0xFCD2)
    out[1] = 0xFC;
    out[2] = kDeviceInfoEncryptedV2;

    // Per BTHome v2: nonce = MAC(LE,6) || UUID(LE,2) || DeviceInfo(1) || Counter(LE,4)
    uint8_t nonce[carl::aes_ccm::kNonceLen];
    std::memcpy(nonce, mac6, 6);
    nonce[6] = 0xD2;
    nonce[7] = 0xFC;
    nonce[8] = kDeviceInfoEncryptedV2;
    writeLE32(nonce + 9, counter);

    uint8_t mic[carl::aes_ccm::kMicLen];
    if (!carl::aes_ccm::encrypt(key16, nonce, plaintext_, plaintext_len_,
                                out + 3, mic)) {
        return false;
    }

    size_t pos = 3 + plaintext_len_;
    writeLE32(out + pos, counter);
    pos += 4;
    std::memcpy(out + pos, mic, carl::aes_ccm::kMicLen);
    pos += carl::aes_ccm::kMicLen;

    *out_len = pos;
    return true;
}

}  // namespace carl::bthome
