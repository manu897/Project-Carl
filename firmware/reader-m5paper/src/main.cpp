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

// Two pages: the card grid (default) and a per-plant detail view with a
// history graph, reached by tapping a card. State lives here in main.cpp —
// dashboard.cpp only draws what it's told to.
enum class Page { kGrid, kDetail };
Page   g_page = Page::kGrid;
size_t g_detail_index = 0;

// Last successfully fetched list — kept around so returning from the detail
// page can redraw the grid without waiting for the next poll, and so
// hitTestCard() has something to test against between polls.
carl::hub::NodeList g_list{};
bool g_have_list = false;

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

// Check for a touch and act on it immediately: switch pages, fetch history
// on entering the detail page, redraw. Called every ~200ms from both the
// main poll cycle and the inter-poll wait so taps feel responsive even
// though network polling itself is slow (10-60s).
void pollTouch() {
    if (!carl::dashboard::consumeTouch()) return;
    int tx = 0, ty = 0;
    carl::dashboard::lastTouchPoint(&tx, &ty);

    if (g_page == Page::kGrid) {
        if (!g_have_list) return;
        const int idx = carl::dashboard::hitTestCard(g_list, tx, ty);
        if (idx < 0) return;  // tap missed every card — ignore

        g_detail_index = static_cast<size_t>(idx);
        g_page = Page::kDetail;

        // Fetched once on entry, not on every poll — history doesn't need
        // second-by-second freshness and a hub fetch shouldn't block the
        // page transition from feeling instant.
        //
        // `static` is deliberate: History is ~4 KB (48 samples), and
        // pollTouch() runs every ~200ms on the Arduino loop task's small
        // default stack (~8 KB) — a plain local here reserves that 4 KB on
        // EVERY call regardless of whether this branch even runs (a
        // function's stack frame is sized for its whole scope, not just
        // the taken branch), stacked on top of loop()'s own ~4 KB NodeList
        // local. That's most of the stack budget gone on nearly every tick
        // — a guaranteed overflow/reboot loop. Static storage moves it to
        // .bss instead; safe here since this task is the only caller.
        static carl::hub::History history{};
        history = carl::hub::History{};
        if (!carl::hub::fetchHistory(g_list.nodes[g_detail_index].id, "24h", &history)) {
            Serial.printf("[carl-reader] history fetch failed: %s\n", history.err_msg);
        }
        carl::dashboard::showNodeDetail(g_list.nodes[g_detail_index], history);
    } else {  // Page::kDetail
        if (carl::dashboard::isBackTouch(tx, ty)) {
            g_page = Page::kGrid;
            if (g_have_list) carl::dashboard::showNodeList(g_list);
        }
    }
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

    pollTouch();  // pick up any tap that landed since the last iteration

    carl::dashboard::showStatus("Fetching plants…");
    // Fetch straight into g_list rather than a local NodeList — that struct
    // is ~4 KB (16 nodes) and this is called every poll on the Arduino loop
    // task's small default stack; see the History comment in pollTouch()
    // for the full stack-overflow story this avoids. Bonus: fetchNodes()
    // doesn't touch count/nodes on its failure paths, so g_list's previous
    // good contents survive a failed poll automatically — "keep last view"
    // for free, no separate copy-on-success step needed.
    const bool ok = carl::hub::fetchNodes(&g_list);

    if (!ok) {
        char msg[80];
        std::snprintf(msg, sizeof(msg), "Hub fetch: %s — keeping last view",
                      g_list.err_msg);
        carl::dashboard::showStatus(msg);
        Serial.printf("[carl-reader] %s\n", msg);
    } else {
        g_have_list = true;

        // Only the grid page redraws on a poll. The detail page deliberately
        // stays on-screen until the user taps "< Back" — a full GC16 refresh
        // every 10-60s while someone's reading the history graph would be
        // distracting on e-paper, and the graph doesn't need that freshness.
        if (g_page == Page::kGrid) {
            const uint32_t h = lastListHash(g_list);
            if (h != last_hash) {
                carl::dashboard::showNodeList(g_list);
                last_hash = h;
                Serial.printf("[carl-reader] redraw — %u plants\n",
                              static_cast<unsigned>(g_list.count));
            } else {
                Serial.println("[carl-reader] no changes — skipping repaint");
            }
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
        pollTouch();  // keep taps responsive during the long inter-poll wait
        delay(200);
    }
}
