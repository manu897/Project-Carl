#include "bthome_emit.h"

#include <cstring>

#include <zephyr/bluetooth/addr.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "bthome.h"
#include "keystore.h"

namespace carl::room::bthome_emit {

namespace {

uint8_t g_mac_le[6] = {0};

// How long to broadcast each cycle. Long enough that the hub's passive scan
// (default ~11.25 ms / 11.25 ms = 100 % duty) catches it many times over.
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

bool broadcastOnce(const carl::room::sensors::Sample& s) {
    carl::bthome::Builder b;
    b.addPacketId(static_cast<uint8_t>(carl::keystore::nextCounter() & 0xFF));
    b.addBattery(s.battery_pct);

    if (s.temp_ok)     b.addTemperature(s.temperature_c);
    if (s.humidity_ok) b.addHumidity(s.humidity_pct);
    if (s.pressure_ok) b.addPressure(s.pressure_hpa);
    if (s.lux_ok)      b.addIlluminance(s.illuminance_lux);

    if (!b.ok()) return false;

    uint8_t svc[carl::bthome::kMaxPlaintext + 11];
    size_t  svc_len = 0;
    if (!b.buildEncrypted(svc, sizeof(svc), &svc_len,
                          carl::keystore::key(), g_mac_le,
                          carl::keystore::nextCounter())) {
        return false;
    }

    // BTHome v2 service data only — no BT_DATA_FLAGS. NCS's legacy-adv path
    // rejects the flags-plus-svc-data pair with -EINVAL, and BTHome scanners
    // only key off the service-data UUID 0xFCD2. See firmware/node-sensor/
    // src/bthome_emit.cpp for the long story behind this.
    const struct bt_data ad[] = {
        BT_DATA(BT_DATA_SVC_DATA16, svc, svc_len),
    };

    // BT_LE_ADV_OPT_USE_IDENTITY is required because we don't enable
    // CONFIG_BT_PRIVACY — without it Zephyr can't generate the RPA it
    // would otherwise want to advertise with and bt_le_adv_start returns
    // -EINVAL on every call. Using the device's static random identity
    // address is what we want anyway: stable across reboots so the hub's
    // MAC↔key map keeps working.
    struct bt_le_adv_param param = BT_LE_ADV_PARAM_INIT(
        BT_LE_ADV_OPT_USE_IDENTITY,
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

}  // namespace carl::room::bthome_emit
