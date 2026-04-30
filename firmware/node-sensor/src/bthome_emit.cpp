#include "bthome_emit.h"

#include <cstring>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "bthome.h"
#include "keystore.h"

namespace carl::bthome_emit {

namespace {

uint8_t g_mac_le[6] = {0};

// How long to broadcast each cycle. Long enough that the hub's passive scan
// (typical 100 ms window every 1 s) catches the advertisement at least once.
constexpr int kBroadcastMs = 200;

void readMacLe() {
    bt_addr_le_t addr{};
    size_t count = 1;
    bt_id_get(&addr, &count);
    if (count == 1) std::memcpy(g_mac_le, addr.a.val, 6);
}

}  // namespace

bool init() {
    // bt_enable() is called from main; we can read identity here.
    readMacLe();
    return true;
}

bool broadcastOnce(const carl::sensors::Sample& s,
                   carl::thresholds::Severity severity) {
    static uint8_t cycle = 0;
    cycle ^= 1;

    carl::bthome::Builder b;
    b.addPacketId(static_cast<uint8_t>(carl::keystore::nextCounter() & 0xFF));
    b.addBattery(s.battery_pct);

    if (cycle == 0) {
        if (s.bme_ok) {
            b.addTemperature(s.temperature_c);
            b.addHumidity(s.humidity_pct);
            b.addPressure(s.pressure_hpa);
        }
    } else {
        if (s.soil_ok)  b.addMoisture(s.soil_pct);
        if (s.veml_ok)  b.addIlluminance(s.illuminance_lux);
    }

    if (!b.ok()) return false;
    (void)severity;  // Carried in a future Carl-namespace extension; for now
                     // the hub derives it from the moisture %.

    uint8_t svc[carl::bthome::kMaxPlaintext + 11];
    size_t  svc_len = 0;
    if (!b.buildEncrypted(svc, sizeof(svc), &svc_len,
                          carl::keystore::key(), g_mac_le,
                          carl::keystore::nextCounter())) {
        return false;
    }

    const struct bt_data ad[] = {
        BT_DATA_BYTES(BT_DATA_FLAGS, BT_LE_AD_NO_BREDR | BT_LE_AD_GENERAL),
        BT_DATA(BT_DATA_SVC_DATA16, svc, svc_len),
    };

    // BT_LE_ADV_NCONN is a C99 compound-literal macro and won't compile in
    // C++ ("taking address of temporary array"). Build the param explicitly.
    struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
        /*options=*/0,
        BT_GAP_ADV_FAST_INT_MIN_2,
        BT_GAP_ADV_FAST_INT_MAX_2,
        /*peer=*/nullptr);
    int rc = bt_le_adv_start(&param, ad, ARRAY_SIZE(ad), nullptr, 0);
    if (rc != 0) {
        printk("bt_le_adv_start failed: %d\n", rc);
        return false;
    }
    k_msleep(kBroadcastMs);
    bt_le_adv_stop();
    return true;
}

}  // namespace carl::bthome_emit
