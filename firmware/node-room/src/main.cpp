// Project-Carl — room-monitor node entry point on the Nordic Thingy:53.
//
// Broadcasts T/H/P/lux/battery via BTHome v2 every CARL_ROOM_SAMPLE_INTERVAL_SEC
// seconds. One per room — the hub joins each plant probe's soil reading with
// the room's environment when serving /api/nodes (#16 in the roadmap).
//
// Boot sequence:
//   1. Init sensors + keystore + BT
//   2. Nokia chime on the piezo buzzer + rainbow LED sweep (combined)
//   3. Print MAC + AES key to USB CDC console
//   4. Green LED flash on every successful broadcast, red on failure

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "bthome_emit.h"
#include "keystore.h"
#include "sensors.h"
#include "ui/buzzer.h"
#include "ui/led.h"

#ifndef CONFIG_CARL_ROOM_SAMPLE_INTERVAL_SEC
#define CONFIG_CARL_ROOM_SAMPLE_INTERVAL_SEC 30
#endif
#ifndef CONFIG_CARL_ROOM_BATTERY_LOW_PCT
#define CONFIG_CARL_ROOM_BATTERY_LOW_PCT 15
#endif

namespace {

const char* airQualityStr(carl::room::sensors::AirQuality aq) {
    using AQ = carl::room::sensors::AirQuality;
    switch (aq) {
        case AQ::kGood:     return "good";
        case AQ::kModerate: return "moderate";
        case AQ::kPoor:     return "poor";
        default:            return "unknown";
    }
}

// Combined boot animation: Nokia tune + rainbow LED sweep.
// Each note of the 13-note tune gets a different colour from the spectrum.
void bootLightShow() {
    struct Step { uint16_t hz; uint16_t ms; uint8_t r, g, b; };
    static constexpr Step kSteps[] = {
        {659, 130, 255,   0,   0},  // E5  — Red
        {587, 130, 255, 100,   0},  // D5  — Orange
        {370, 260, 255, 255,   0},  // F#4 — Yellow
        {415, 260,   0, 255,   0},  // G#4 — Green
        {554, 130,   0, 255, 128},  // C#5 — Spring
        {494, 130,   0, 255, 255},  // B4  — Cyan
        {294, 260,   0, 128, 255},  // D4  — Azure
        {330, 260,   0,   0, 255},  // E4  — Blue
        {494, 130, 128,   0, 255},  // B4  — Violet
        {440, 130, 255,   0, 255},  // A4  — Magenta
        {554, 260, 255,   0, 128},  // C#5 — Rose
        {659, 260, 255, 200, 200},  // E5  — Salmon
        {440, 520, 255, 255, 255},  // A4  — White (finale)
    };

    for (const auto& s : kSteps) {
        carl::room::ui::led::setRGB(s.r, s.g, s.b);
        carl::room::ui::buzzer::tone(s.hz, s.ms);
        k_msleep(20);  // note articulation gap
    }
    carl::room::ui::led::off();
}

void printCredentials() {
    // Always print MAC (not secret). Always print key during bring-up so
    // the user can grab it after any reset — not just first boot. Gate
    // behind a Kconfig flag when shipping production builds.
    bt_addr_le_t addr{};
    size_t addr_count = 1;
    bt_id_get(&addr, &addr_count);
    if (addr_count == 1) {
        const auto* m = addr.a.val;
        printk("\n============================================\n");
        printk("room: MAC  %02X:%02X:%02X:%02X:%02X:%02X\n",
               m[5], m[4], m[3], m[2], m[1], m[0]);
    }
    char hex[33];
    carl::keystore::keyHex(hex);
    printk("room: KEY  %s\n", hex);
    printk("============================================\n\n");
}

void boot() {
    settings_subsys_init();
    settings_load();

    // --- Sensors ---
    if (!carl::room::sensors::init()) {
        printk("room: no sensors initialised — broadcasts will only carry battery\n");
    }

    // --- Keystore ---
    if (!carl::keystore::init()) {
        printk("room: keystore_init failed\n");
    }

    // --- UI ---
    carl::room::ui::led::init();
    carl::room::ui::buzzer::init();

    // --- Bluetooth ---
    if (bt_enable(nullptr) != 0) {
        printk("room: bt_enable failed\n");
        carl::room::ui::buzzer::errorBeep();
        carl::room::ui::led::flashRed(500);
    }
    carl::room::bthome_emit::init();

    // --- Boot animation: Nokia chime + rainbow LED ---
    bootLightShow();

    // --- Credentials (always printed during bring-up) ---
    printCredentials();
}

}  // namespace

int main(void) {
    boot();

    uint32_t sample_no = 0;
    while (true) {
        carl::room::sensors::Sample s{};
        carl::room::sensors::sample(&s);

        printk("room sample #%u: T=%.1fC H=%.0f%% P=%.1fhPa lux=%.0f "
               "gas=%uohm(%s) bat=%u%% (%umV)\n",
               static_cast<unsigned>(sample_no),
               s.temp_ok      ? static_cast<double>(s.temperature_c)   : 0.0,
               s.humidity_ok  ? static_cast<double>(s.humidity_pct)    : 0.0,
               s.pressure_ok  ? static_cast<double>(s.pressure_hpa)   : 0.0,
               s.lux_ok       ? static_cast<double>(s.illuminance_lux) : 0.0,
               static_cast<unsigned>(s.gas_resistance_ohm),
               airQualityStr(s.air_quality),
               static_cast<unsigned>(s.battery_pct),
               static_cast<unsigned>(carl::room::sensors::batteryMv()));

        if (carl::room::bthome_emit::broadcastOnce(s)) {
            // Layer status alerts onto the success blink so a glance at the
            // node signals "needs attention" without checking the dashboard.
            // Poor air takes priority (rarer, more actionable); battery-low
            // is the common case; otherwise plain green — same idea as the
            // plant node's severity blink.
            using AQ = carl::room::sensors::AirQuality;
            const bool battery_low = s.battery_pct > 0
                                    && s.battery_pct <= CONFIG_CARL_ROOM_BATTERY_LOW_PCT;
            if (s.air_quality == AQ::kPoor) {
                carl::room::ui::led::blinkStatus(140, 0, 200, 2);  // 2x purple — poor air
            } else if (battery_low) {
                carl::room::ui::led::blinkStatus(255, 140, 0, 1);  // 1x amber — battery low
            } else {
                carl::room::ui::led::flashGreen();  // all clear
            }
        } else {
            printk("room: broadcast failed\n");
            carl::room::ui::led::flashRed();     // error — red flash
            carl::room::ui::buzzer::errorBeep();
        }

        ++sample_no;
        k_sleep(K_SECONDS(CONFIG_CARL_ROOM_SAMPLE_INTERVAL_SEC));
    }
    return 0;
}
