// Project-Carl — M5Paper reader-node entry point.
//
// Architecture: Wi-Fi client of the Carl hub. Polls
//   GET http://carl-hub.local/api/nodes
// every CARL_READER_POLL_SEC, renders the per-plant cards on the e-paper,
// goes light-sleep between polls. The hub stays the single source of truth
// (BLE scanning, AES key storage, history, cloud handoff). The reader is a
// premium-tier always-on dashboard.
//
// Mock mode (-DCARL_READER_USE_MOCK) lets the firmware run with no hub and
// no router — the hub_client returns a hardcoded JSON. Useful for bench
// bring-up of the e-paper / touch / layout while the hub firmware is still
// being built (Phase 3).

#include <Arduino.h>
#include <ESPmDNS.h>   // for resolving carl-hub.local
#include <M5EPD.h>     // for the M5.update() ticking call in loop()
#include <time.h>      // configTzTime / time() for the header clock

#include "dashboard.h"
#include "hub_client.h"
#include "wifi_setup.h"

#ifndef CARL_READER_POLL_SEC
// 60 s during bring-up so changes to the hub state surface quickly. Once
// everything's stable, bump to 300 (5 min) for the e-paper refresh budget.
#define CARL_READER_POLL_SEC 60
#endif

#ifndef CARL_READER_TZ
// POSIX-style TZ string. Override with -DCARL_READER_TZ='"EST5EDT,M3.2.0,M11.1.0"'
// (or your local rule) in platformio.ini. Default keeps things obvious if no
// TZ has been set: UTC.
#define CARL_READER_TZ "UTC0"
#endif

namespace {

uint32_t lastListHash(const carl::hub::NodeList& list) {
    // Cheap content fingerprint — enough to skip a redundant e-paper
    // refresh when nothing meaningful changed since last poll. Hashes the
    // soil_pct, online, and last-seen timestamp of every node. Not a
    // cryptographic hash; collisions just mean a missed redraw which the
    // next real change will repaint.
    uint32_t h = 2166136261u;
    auto mix = [&](const void* p, size_t n) {
        const uint8_t* b = static_cast<const uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            h ^= b[i];
            h *= 16777619u;
        }
    };
    mix(&list.count, sizeof(list.count));
    for (size_t i = 0; i < list.count; ++i) {
        const auto& n = list.nodes[i];
        mix(n.last_seen_iso, sizeof(n.last_seen_iso));
        mix(&n.online, sizeof(n.online));
        mix(&n.latest.has_soil_pct, sizeof(n.latest.has_soil_pct));
        mix(&n.latest.soil_pct, sizeof(n.latest.soil_pct));
    }
    return h;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println("\n[carl-reader] boot");

    carl::dashboard::init();
    carl::dashboard::showBootSplash();

    const bool wifi_up = carl::wifi::ensureConnected();
    if (!wifi_up) {
        carl::dashboard::showStatus("Wi-Fi unavailable — running offline");
    } else {
        if (!MDNS.begin("carl-reader")) {
            Serial.println("[mdns] init failed");
        }
        // Kick off NTP so the header clock starts ticking. configTzTime
        // returns immediately; the SNTP task fills in the time over the
        // next few seconds. formatNow() in dashboard.cpp falls back to
        // "Plant monitor" until the first sync lands.
        configTzTime(CARL_READER_TZ, "pool.ntp.org", "time.nist.gov");
        carl::dashboard::showStatus("Wi-Fi connected");
    }

    // Animated welcome — "Carl / Plant monitor" with potted plants
    // growing on each side. ~3 s total, no user interaction required.
    carl::dashboard::showWelcomeSplash();

    // Drop straight into the empty dashboard so the canvas is the right
    // shape before the first hub-fetch happens (which may fail and just
    // update the status line — without this we'd keep the welcome on
    // screen indefinitely).
    {
        carl::hub::NodeList empty{};
        empty.count    = 0;
        empty.fetch_ok = true;
        carl::dashboard::showNodeList(empty);
    }
    carl::dashboard::showStatus("Fetching plants...");
}

void loop() {
    static uint32_t last_hash = 0;

    carl::dashboard::showStatus("Fetching plants…");
    carl::hub::NodeList list{};
    const bool ok = carl::hub::fetchNodes(&list);

    if (!ok) {
        char msg[80];
        std::snprintf(msg, sizeof(msg), "Hub fetch: %s — keeping last view",
                      list.err_msg);
        carl::dashboard::showStatus(msg);
        Serial.printf("[carl-reader] %s\n", msg);
    } else {
        const uint32_t h = lastListHash(list);
        if (h != last_hash) {
            carl::dashboard::showNodeList(list);
            last_hash = h;
            Serial.printf("[carl-reader] redraw — %u plants\n",
                          static_cast<unsigned>(list.count));
        } else {
            Serial.println("[carl-reader] no changes — skipping repaint");
        }
        carl::dashboard::showStatus("Up to date");
    }

    M5.update();
    // Poll cadence depends on power state. On battery, slow polls preserve
    // the e-paper refresh budget + battery life. On USB / charging, fast
    // polls give live feedback (the e-paper refresh budget isn't a concern
    // when plugged in).
    const uint32_t poll_sec = carl::dashboard::isCharging()
                              ? 10u
                              : static_cast<uint32_t>(CARL_READER_POLL_SEC);
    const uint32_t target = millis() + poll_sec * 1000UL;
    while (millis() < target) {
        M5.update();
        delay(200);
    }
}
