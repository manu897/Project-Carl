// NimBLE-based BLE central scanner. Walks every advertisement, looks for
// the BTHome v2 service-data AD struct (UUID 0xFCD2), and hands the
// payload to the shared decoder in firmware/common/bthome/.
//
// Compiled as C++ so it can call the carl::bthome:: namespaced decoder
// directly. The init function is `extern "C"` so main.c can call it.

#include "ble_scanner.h"

#include <cstring>

extern "C" {
#include "esp_log.h"
#include "esp_timer.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
}

#include "bthome.h"
#include "bthome_decode.h"
#include "key_store.h"
#include "node_registry.h"

static const char *TAG = "carl-ble";

namespace {

// Visitor state: one per advertisement, populates a carl_reading_t from
// the BTHome objects yielded by the decoder.
struct VisitState {
    carl_reading_t r;
    uint32_t       counter;
    uint8_t        mac_le[6];
};

bool visitMeasurement(const carl::bthome::Measurement &m, void *user) {
    auto *st = static_cast<VisitState *>(user);
    using carl::bthome::ObjectId;
    switch (m.id) {
        case ObjectId::kPacketId:
            st->r.packet_id = m.asPacketId();
            break;
        case ObjectId::kBattery:
            st->r.battery_ok  = true;
            st->r.battery_pct = m.asBattery();
            break;
        case ObjectId::kTemperature:
            st->r.temp_ok = true;
            st->r.temp_c  = m.asTemperatureC();
            break;
        case ObjectId::kHumidity:
            st->r.humidity_ok  = true;
            st->r.humidity_pct = m.asHumidityPct();
            break;
        case ObjectId::kPressure:
            st->r.pressure_ok  = true;
            st->r.pressure_hpa = m.asPressureHpa();
            break;
        case ObjectId::kIlluminance:
            st->r.lux_ok = true;
            st->r.lux    = m.asIlluminance();
            break;
        case ObjectId::kMoisture:
            st->r.soil_ok  = true;
            st->r.soil_pct = m.asMoisturePct();
            break;
    }
    return true;
}

// Walk the AD-structure list looking for service data with UUID 0xFCD2.
// Returns pointer to the start of the service-data value (UUID16 LE +
// payload) and its length, or {nullptr, 0} if not present.
struct BthomeAd { const uint8_t *data; size_t len; };

BthomeAd findBthomeServiceData(const uint8_t *ad, uint8_t ad_len) {
    size_t i = 0;
    while (i < ad_len) {
        const uint8_t struct_len = ad[i];
        if (struct_len == 0 || i + struct_len >= ad_len) break;
        const uint8_t type = ad[i + 1];
        // 0x16 = Service Data (16-bit UUID). UUID is LE: 0xD2 0xFC.
        if (type == 0x16 && struct_len >= 3
            && ad[i + 2] == 0xD2 && ad[i + 3] == 0xFC) {
            return { &ad[i + 2], static_cast<size_t>(struct_len - 1) };
        }
        i += struct_len + 1;
    }
    return { nullptr, 0 };
}

void handleAdvertisement(const struct ble_gap_disc_desc *d) {
    // Counters + heartbeat so we know whether the BLE radio is actually
    // receiving anything. If `total` stays at 0 after 30 s, the controller
    // isn't running scans (despite the "BLE scan started" log line).
    static uint32_t total_ads  = 0;
    static uint32_t bthome_ads = 0;
    static int64_t  last_log_ms = 0;
    static bool     logged_first = false;

    total_ads++;
    if (!logged_first) {
        ESP_LOGI(TAG, "first ad received: rssi=%d, %u bytes of data",
                 d->rssi, (unsigned)d->length_data);
        logged_first = true;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    if (now_ms - last_log_ms > 30000) {
        ESP_LOGI(TAG, "scan stats: %u total ads, %u BTHome",
                 (unsigned)total_ads, (unsigned)bthome_ads);
        last_log_ms = now_ms;
    }

    BthomeAd sd = findBthomeServiceData(d->data, d->length_data);
    if (sd.data == nullptr) return;
    bthome_ads++;

    // d->addr.val is little-endian, NimBLE convention. Look up key.
    const uint8_t *key = carl_key_store_lookup(d->addr.val);
    if (key == nullptr) {
        // Unknown source. Bumped to INFO during 3b bring-up so we can see
        // whether BTHome ads are actually arriving when nothing decodes.
        // Once decryption works for a known sensor, drop this back to LOGV
        // so neighbour BTHome devices (Shelly, Xiaomi, ESPHome, etc.)
        // don't spam the log.
        ESP_LOGI(TAG, "BTHome ad from %02X:%02X:%02X:%02X:%02X:%02X (no key, sd_len=%u)",
                 d->addr.val[5], d->addr.val[4], d->addr.val[3],
                 d->addr.val[2], d->addr.val[1], d->addr.val[0],
                 (unsigned)sd.len);
        return;
    }

    VisitState st{};
    std::memcpy(st.mac_le, d->addr.val, 6);
    carl::bthome::DecodedHeader hdr{};
    auto status = carl::bthome::decodeEncrypted(
        sd.data, sd.len, key, d->addr.val, &visitMeasurement, &st, &hdr);
    if (status != carl::bthome::DecodeStatus::kOk) {
        ESP_LOGW(TAG, "decode failed (%d) for %02X:%02X:%02X:%02X:%02X:%02X",
                 static_cast<int>(status),
                 d->addr.val[5], d->addr.val[4], d->addr.val[3],
                 d->addr.val[2], d->addr.val[1], d->addr.val[0]);
        return;
    }
    carl_node_registry_update(d->addr.val, hdr.counter, &st.r);
    ESP_LOGI(TAG, "ad ok mac=%02X:%02X:%02X:%02X:%02X:%02X ctr=%lu "
                  "T=%s H=%s P=%s soil=%s lux=%s bat=%s",
             d->addr.val[5], d->addr.val[4], d->addr.val[3],
             d->addr.val[2], d->addr.val[1], d->addr.val[0],
             (unsigned long)hdr.counter,
             st.r.temp_ok ? "y" : "-",
             st.r.humidity_ok ? "y" : "-",
             st.r.pressure_ok ? "y" : "-",
             st.r.soil_ok ? "y" : "-",
             st.r.lux_ok ? "y" : "-",
             st.r.battery_ok ? "y" : "-");
}

int gapEvent(struct ble_gap_event *event, void *arg) {
    if (event->type == BLE_GAP_EVENT_DISC) {
        handleAdvertisement(&event->disc);
    }
    return 0;
}

void startScan() {
    struct ble_gap_disc_params p{};
    p.itvl              = 0;     // default 11.25 ms
    p.window            = 0;     // default 11.25 ms
    p.filter_policy     = 0;
    p.limited           = 0;
    p.passive           = 1;     // we only want advertisements, never connect
    p.filter_duplicates = 0;
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &p, gapEvent, nullptr);
    if (rc != 0) ESP_LOGE(TAG, "ble_gap_disc rc=%d", rc);
    else ESP_LOGI(TAG, "BLE scan started — listening for BTHome");
}

void onSync() {
    // Standard NimBLE init order: make sure the controller has an
    // identity address, then ask the host to pick which address type
    // (public / RPA / random) to use. The second arg to
    // ble_hs_id_infer_auto MUST be a real pointer — passing nullptr
    // causes a NULL deref deep inside ble_hs_id.c.
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_util_ensure_addr rc=%d", rc);
        return;
    }
    uint8_t own_addr_type = 0;
    rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_hs_id_infer_auto rc=%d", rc);
        return;
    }
    startScan();
}

void onReset(int reason) {
    ESP_LOGW(TAG, "host reset, reason %d", reason);
}

void hostTask(void *) {
    nimble_port_run();
    nimble_port_freertos_deinit();
}

}  // namespace

extern "C" void carl_ble_scanner_start(void) {
    nimble_port_init();
    ble_hs_cfg.sync_cb  = onSync;
    ble_hs_cfg.reset_cb = onReset;
    ble_svc_gap_device_name_set("carl-hub");
    nimble_port_freertos_init(hostTask);
}
