// Host round-trip test for the BTHome v2 encoder + decoder.
//
// Build + run:
//   cd firmware/common/test && make test
//
// Requires system mbedtls (3.x):
//   macOS:  brew install mbedtls
//   Debian: apt install libmbedtls-dev

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "bthome.h"
#include "bthome_decode.h"

using namespace carl::bthome;

namespace {

struct Captured {
    std::vector<Measurement> objects;
};

bool collectVisitor(const Measurement& m, void* user) {
    static_cast<Captured*>(user)->objects.push_back(m);
    return true;
}

void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        std::exit(1);
    }
}

void approxEq(float got, float want, float tol, const char* name) {
    const float diff = std::fabs(got - want);
    if (diff > tol) {
        std::fprintf(stderr, "FAIL approxEq %s: got=%.4f want=%.4f tol=%.4f\n",
                     name, got, want, tol);
        std::exit(1);
    }
}

const uint8_t kKey[16] = {
    0x23, 0x1d, 0x39, 0xc1, 0xd7, 0xcc, 0x1a, 0xb1,
    0xae, 0xe2, 0x24, 0xcd, 0x09, 0x6d, 0xb9, 0x32
};
const uint8_t kMac[6] = { 0x54, 0x48, 0xe6, 0x8f, 0x80, 0xa5 };

void test_round_trip() {
    Builder b;
    check(b.addPacketId(7),         "addPacketId");
    check(b.addBattery(83),         "addBattery");
    check(b.addTemperature(21.34f), "addTemperature");
    check(b.addHumidity(48.50f),    "addHumidity");
    check(b.addPressure(1013.25f),  "addPressure");
    check(b.addIlluminance(120.0f), "addIlluminance");
    check(b.addMoisture(32.50f),    "addMoisture");
    check(b.ok(),                   "builder ok");

    uint8_t out[kMaxPlaintext + 11];
    size_t out_len = 0;
    check(b.buildEncrypted(out, sizeof(out), &out_len, kKey, kMac, /*counter=*/42),
          "buildEncrypted");
    check(out_len > 11, "out_len plausible");

    // Header sanity check.
    check(out[0] == 0xD2 && out[1] == 0xFC, "UUID16 LE on the wire");
    check(out[2] == kDeviceInfoEncryptedV2, "DeviceInfo byte");

    Captured cap;
    DecodedHeader hdr{};
    DecodeStatus s = decodeEncrypted(out, out_len, kKey, kMac,
                                     collectVisitor, &cap, &hdr);
    check(s == DecodeStatus::kOk, "decode ok");
    check(hdr.counter == 42, "counter recovered");
    check(cap.objects.size() == 7, "7 objects decoded");

    check(cap.objects[0].id == kPacketId, "[0] id");
    check(cap.objects[0].asPacketId() == 7, "[0] packetId");

    check(cap.objects[1].id == kBattery, "[1] id");
    check(cap.objects[1].asBattery() == 83, "[1] battery");

    check(cap.objects[2].id == kTemperature, "[2] id");
    approxEq(cap.objects[2].asTemperatureC(), 21.34f, 0.011f, "temperature");

    check(cap.objects[3].id == kHumidity, "[3] id");
    approxEq(cap.objects[3].asHumidityPct(), 48.50f, 0.011f, "humidity");

    check(cap.objects[4].id == kPressure, "[4] id");
    approxEq(cap.objects[4].asPressureHpa(), 1013.25f, 0.011f, "pressure");

    check(cap.objects[5].id == kIlluminance, "[5] id");
    approxEq(cap.objects[5].asIlluminance(), 120.0f, 0.011f, "illuminance");

    check(cap.objects[6].id == kMoisture, "[6] id");
    approxEq(cap.objects[6].asMoisturePct(), 32.50f, 0.011f, "moisture");

    std::printf("PASS test_round_trip\n");
}

void test_negative_temperature() {
    Builder b;
    check(b.addTemperature(-12.75f), "addTemperature negative");

    uint8_t out[kMaxPlaintext + 11];
    size_t out_len = 0;
    check(b.buildEncrypted(out, sizeof(out), &out_len, kKey, kMac, 1), "build");

    Captured cap;
    check(decodeEncrypted(out, out_len, kKey, kMac, collectVisitor, &cap, nullptr)
          == DecodeStatus::kOk, "decode");
    check(cap.objects.size() == 1, "one object");
    approxEq(cap.objects[0].asTemperatureC(), -12.75f, 0.011f, "negative temp");

    std::printf("PASS test_negative_temperature\n");
}

void test_wrong_key_fails_auth() {
    Builder b;
    check(b.addBattery(60), "addBattery");

    uint8_t out[kMaxPlaintext + 11];
    size_t out_len = 0;
    check(b.buildEncrypted(out, sizeof(out), &out_len, kKey, kMac, 1), "build");

    uint8_t wrong[16];
    std::memcpy(wrong, kKey, 16);
    wrong[0] ^= 0x01;

    Captured cap;
    DecodeStatus s = decodeEncrypted(out, out_len, wrong, kMac,
                                     collectVisitor, &cap, nullptr);
    check(s == DecodeStatus::kAuthFailed, "wrong key -> kAuthFailed");
    check(cap.objects.empty(), "no objects on auth fail");

    std::printf("PASS test_wrong_key_fails_auth\n");
}

void test_tampered_payload_fails_auth() {
    Builder b;
    check(b.addBattery(60), "addBattery");

    uint8_t out[kMaxPlaintext + 11];
    size_t out_len = 0;
    check(b.buildEncrypted(out, sizeof(out), &out_len, kKey, kMac, 1), "build");

    // Flip a byte in the ciphertext region (after UUID + DeviceInfo, before MIC).
    out[3] ^= 0xFF;

    Captured cap;
    DecodeStatus s = decodeEncrypted(out, out_len, kKey, kMac,
                                     collectVisitor, &cap, nullptr);
    check(s == DecodeStatus::kAuthFailed, "tampered ct -> kAuthFailed");

    std::printf("PASS test_tampered_payload_fails_auth\n");
}

void test_distinct_counters_produce_distinct_ciphertexts() {
    Builder b1;
    check(b1.addBattery(60), "build1");
    Builder b2;
    check(b2.addBattery(60), "build2");

    uint8_t out1[kMaxPlaintext + 11], out2[kMaxPlaintext + 11];
    size_t l1 = 0, l2 = 0;
    check(b1.buildEncrypted(out1, sizeof(out1), &l1, kKey, kMac, 1), "build1");
    check(b2.buildEncrypted(out2, sizeof(out2), &l2, kKey, kMac, 2), "build2");
    check(l1 == l2, "lengths equal");
    // Ciphertext + counter+MIC differ (header is identical).
    check(std::memcmp(out1 + 3, out2 + 3, l1 - 3) != 0,
          "different counters -> different ciphertext+MIC");

    std::printf("PASS test_distinct_counters_produce_distinct_ciphertexts\n");
}

void test_buffer_too_small() {
    Builder b;
    for (int i = 0; i < 8; ++i) b.addTemperature(20.0f + i);

    uint8_t out[8];  // intentionally too small
    size_t out_len = 0;
    check(!b.buildEncrypted(out, sizeof(out), &out_len, kKey, kMac, 1),
          "build refuses too-small buffer");

    std::printf("PASS test_buffer_too_small\n");
}

}  // namespace

int main() {
    test_round_trip();
    test_negative_temperature();
    test_wrong_key_fails_auth();
    test_tampered_payload_fails_auth();
    test_distinct_counters_produce_distinct_ciphertexts();
    test_buffer_too_small();
    std::printf("all tests passed\n");
    return 0;
}
