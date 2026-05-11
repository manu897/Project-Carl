#include "dashboard.h"

#include <cstdio>
#include <cstring>
#include <ctime>

#include <M5EPD.h>

namespace carl::dashboard {

namespace {

// M5Paper is natively 960×540 landscape (4.7" e-paper, 16-level grayscale).
// SetRotation(0) keeps that native orientation; previous attempts were
// passing the wrong canvas dimensions and ending up with content squeezed
// into the left 540 px.
constexpr int kScreenW = 960;
constexpr int kScreenH = 540;

// Layout: header strip, 2-column card grid filling middle, status footer.
constexpr int kHeaderH    = 76;
constexpr int kStatusH    = 36;
constexpr int kCardListY  = kHeaderH + 8;
constexpr int kCardListH  = kScreenH - kCardListY - kStatusH - 8;
constexpr int kCardCols   = 2;
constexpr int kCardGap    = 14;
constexpr int kSidePad    = 18;

M5EPD_Canvas g_canvas(&M5.EPD);
M5EPD_Canvas g_status(&M5.EPD);

// Format current wall-clock time into "Mon May 10  14:32". Returns false
// if the system clock hasn't been NTP-synced yet (tm_year before 2020),
// so the caller can fall back to the static subtitle.
bool formatNow(char* buf, size_t cap) {
    time_t now = std::time(nullptr);
    struct tm tm;
    localtime_r(&now, &tm);
    if (tm.tm_year < 120) {  // years since 1900; <120 means pre-2020 = unsynced
        return false;
    }
    std::strftime(buf, cap, "%a %b %d  %H:%M", &tm);
    return true;
}

// Convert M5.getBatteryVoltage() (mV, ~3300–4200 range for a healthy LiPo)
// into a friendly 0–100% used by the header indicator.
uint8_t batteryPct() {
    const uint32_t mv = M5.getBatteryVoltage();
    if (mv == 0) return 0;
    if (mv >= 4200) return 100;
    if (mv <= 3300) return 0;
    return static_cast<uint8_t>((mv - 3300) * 100 / 900);
}


const char* severityFor(const carl::hub::Reading& r) {
    if (!r.has_soil_pct) return "—";
    if (r.soil_pct < 15) return "CRITICAL";
    if (r.soil_pct < 30) return "DRY";
    return "OK";
}

void drawHeader(M5EPD_Canvas& c) {
    char buf[40];

    // Brand top-left.
    c.setTextSize(4);
    c.setTextDatum(TL_DATUM);
    c.drawString("Carl", kSidePad, 14);

    // Center: live date+time once NTP has synced, else the static subtitle
    // so the bar isn't blank during the first few seconds after boot.
    c.setTextDatum(TC_DATUM);
    if (formatNow(buf, sizeof(buf))) {
        c.setTextSize(3);
        c.drawString(buf, kScreenW / 2, 22);
    } else {
        c.setTextSize(2);
        c.drawString("Plant monitor", kScreenW / 2, 28);
    }

    // Battery / charging indicator top-right. "CHG" while plugged in
    // tells the user the device is on USB power and refresh is faster.
    c.setTextSize(2);
    c.setTextDatum(TR_DATUM);
    std::snprintf(buf, sizeof(buf),
                  isCharging() ? "CHG %u%%" : "Bat %u%%",
                  batteryPct());
    c.drawString(buf, kScreenW - kSidePad, 28);

    c.drawFastHLine(kSidePad, kHeaderH - 8, kScreenW - 2 * kSidePad, 15);
}

void drawCard(M5EPD_Canvas& c, const carl::hub::Node& n,
              int x, int y, int w, int h) {
    char buf[40];

    // Frame
    c.drawRect(x, y, w, h, 15);

    // Top strip: name (left) + online/offline (right).
    c.setTextSize(3);
    c.setTextDatum(TL_DATUM);
    c.drawString(n.name, x + 12, y + 10);
    c.setTextSize(2);
    c.setTextDatum(TR_DATUM);
    c.drawString(n.online ? "online" : "offline", x + w - 12, y + 18);

    // Big moisture % — primary at-a-glance reading.
    c.setTextSize(7);
    c.setTextDatum(TL_DATUM);
    if (n.latest.has_soil_pct) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", n.latest.soil_pct);
    } else {
        std::snprintf(buf, sizeof(buf), "--");
    }
    c.drawString(buf, x + 14, y + 50);

    // Severity tag — opposite corner from the moisture %.
    c.setTextSize(4);
    c.setTextDatum(TR_DATUM);
    c.drawString(severityFor(n.latest), x + w - 14, y + 60);

    // Bottom strip: T / H / battery — only fields actually present.
    c.setTextSize(2);
    c.setTextDatum(TL_DATUM);
    int sx = x + 12;
    const int sy = y + h - 28;
    if (n.latest.has_temperature_c) {
        std::snprintf(buf, sizeof(buf), "T %.1fC", n.latest.temperature_c);
        c.drawString(buf, sx, sy); sx += 110;
    }
    if (n.latest.has_humidity_pct) {
        std::snprintf(buf, sizeof(buf), "H %.0f%%", n.latest.humidity_pct);
        c.drawString(buf, sx, sy); sx += 110;
    }
    if (n.has_battery_pct) {
        std::snprintf(buf, sizeof(buf), "Bat %u%%", n.battery_pct);
        c.drawString(buf, sx, sy);
    }
}

}  // namespace

// Forward declaration — drawPlant is defined after showWelcomeSplash
// in this file but is called from it.
void drawPlant(M5EPD_Canvas& c, int cx, int base_y, int stage);

// A LiPo at rest never floats above ~4.15 V — anything higher means the
// charger is actively pushing, i.e. the device is on USB power. Best
// approximation we have without a dedicated VBUS-sense pin.
bool isCharging() {
    return M5.getBatteryVoltage() > 4150;
}

void init() {
    M5.begin();
    M5.EPD.SetRotation(0);   // M5Paper's native is 960×540 landscape.
                              // No rotation needed; canvas matches panel.
    M5.EPD.Clear(true);
    M5.RTC.begin();
    M5.BatteryADCBegin();

    g_canvas.createCanvas(kScreenW, kScreenH);
    g_canvas.setTextSize(3);

    g_status.createCanvas(kScreenW, kStatusH);
    g_status.setTextSize(2);
}

void showBootSplash() {
    g_canvas.fillCanvas(0);
    drawHeader(g_canvas);
    g_canvas.setTextDatum(TC_DATUM);
    g_canvas.setTextSize(4);
    g_canvas.drawString("Connecting...", kScreenW / 2, kScreenH / 2 - 30);
    g_canvas.setTextSize(2);
    g_canvas.drawString("Carl-Reader-Setup AP if first boot",
                        kScreenW / 2, kScreenH / 2 + 40);
    g_canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

void showWelcomeSplash() {
    constexpr int kFrames    = 8;       // stages 0..7 (last 3 = flower bloom)
    constexpr int kFrameMs   = 350;
    constexpr int kHoldMs    = 1200;    // pause on the final frame

    // Plant pots flank the wordmark. base_y is the bottom of each pot.
    const int base_y = kScreenH / 2 + 120;
    const int left_x  = kScreenW / 2 - 220;
    const int right_x = kScreenW / 2 + 220;

    for (int stage = 0; stage < kFrames; ++stage) {
        g_canvas.fillCanvas(0);

        // Big "Carl" centered, with subtitle below.
        g_canvas.setTextDatum(TC_DATUM);
        g_canvas.setTextSize(8);
        g_canvas.drawString("Carl", kScreenW / 2, kScreenH / 2 - 100);
        g_canvas.setTextSize(3);
        g_canvas.drawString("Plant monitor", kScreenW / 2, kScreenH / 2 - 10);

        // Two plants growing in lockstep on either side of the text.
        drawPlant(g_canvas, left_x,  base_y, stage);
        drawPlant(g_canvas, right_x, base_y, stage);

        // First frame uses GC16 (full grayscale, ~700 ms) to set a clean
        // baseline. Subsequent frames use A2 (mono, ~120 ms) so the
        // animation feels responsive. We re-push GC16 at the end to clear
        // any A2 ghosting before the dashboard takes over.
        const m5epd_update_mode_t mode = (stage == 0)
                                         ? UPDATE_MODE_GC16
                                         : UPDATE_MODE_A2;
        g_canvas.pushCanvas(0, 0, mode);
        delay(kFrameMs);
    }

    // Hold the final frame, then promote to GC16 so the canvas is clean
    // before showNodeList() draws over it.
    delay(kHoldMs);
    g_canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

// Render a potted plant at growth stage 0..7 with its base at (cx, base_y).
// 0: empty pot, 1: sprout, 2-4: stem + leaves growing in,
// 5: tight bud, 6: half-opened bloom, 7: full open flower.
// Uses primitives only — no bitmap assets — small + fast.
void drawPlant(M5EPD_Canvas& c, int cx, int base_y, int stage) {
    constexpr int kPotW = 56;
    constexpr int kPotH = 36;

    // Pot — always visible.
    c.fillRect(cx - kPotW / 2, base_y - kPotH, kPotW, kPotH, 12);
    c.drawRect(cx - kPotW / 2, base_y - kPotH, kPotW, kPotH, 15);
    c.drawFastHLine(cx - kPotW / 2 + 2, base_y - kPotH + 6, kPotW - 4, 15);

    if (stage <= 0) return;

    // Stem grows with stage but caps at stage 4 — after that the flower
    // takes over the visual and the stem stops growing.
    const int growth = (stage > 4) ? 4 : stage;
    const int stem_h = 18 + growth * 22;            // tops out at 106 px
    const int stem_top = base_y - kPotH - stem_h;
    c.fillRect(cx - 2, stem_top, 4, stem_h, 15);

    if (stage >= 2) {
        const int ly = stem_top + stem_h * 2 / 3;
        c.fillCircle(cx - 16, ly,     12, 15);
        c.fillCircle(cx - 22, ly + 6,  8, 15);
    }
    if (stage >= 3) {
        const int ly = stem_top + stem_h / 2;
        c.fillCircle(cx + 16, ly,     12, 15);
        c.fillCircle(cx + 22, ly + 6,  8, 15);
    }
    if (stage >= 4) {
        const int ly = stem_top + stem_h / 4;
        c.fillCircle(cx - 14, ly, 10, 15);
    }

    // Flower opens in three frames at the end of the sequence.
    if (stage == 5) {
        // Tight bud: single small circle perched on the stem top.
        c.fillCircle(cx, stem_top - 6, 7, 15);
    } else if (stage == 6) {
        // Half-opened: three petals visible, center forming.
        c.fillCircle(cx,      stem_top - 10, 7, 15);  // top petal
        c.fillCircle(cx - 10, stem_top + 2,  7, 15);  // left petal
        c.fillCircle(cx + 10, stem_top + 2,  7, 15);  // right petal
        c.fillCircle(cx,      stem_top,      4, 15);  // center
    } else if (stage >= 7) {
        // Full bloom: six petals fanned around a darker center.
        // Pre-computed unit vectors at 60° intervals × radius 13 px.
        static const int dx[6] = { 13,  6, -6, -13, -6,  6 };
        static const int dy[6] = {  0, 11, 11,   0,-11,-11 };
        for (int i = 0; i < 6; ++i) {
            c.fillCircle(cx + dx[i], stem_top + dy[i], 8, 15);
        }
        // Bright center disc to suggest the open flower's eye.
        c.fillCircle(cx, stem_top, 6, 15);
        c.drawCircle(cx, stem_top, 8, 0);   // light ring around center
    }
}

bool consumeTouch() {
    if (M5.TP.available()) {
        M5.TP.update();
        // Look for a finger-down edge: state goes from up to down.
        static bool was_down = false;
        const bool down = !M5.TP.isFingerUp();
        const bool edge = down && !was_down;
        was_down = down;
        return edge;
    }
    return false;
}

void showNodeList(const carl::hub::NodeList& list) {
    g_canvas.fillCanvas(0);
    drawHeader(g_canvas);

    if (list.count == 0) {
        g_canvas.setTextDatum(TC_DATUM);
        g_canvas.setTextSize(5);
        g_canvas.drawString("No plants yet",
                            kScreenW / 2, kScreenH / 2 - 30);
        g_canvas.setTextSize(2);
        g_canvas.drawString("Provision a sensor in the hub",
                            kScreenW / 2, kScreenH / 2 + 50);
        g_canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
        return;
    }

    // 2-column × up-to-3-row grid filling the landscape panel. Cards
    // expand to use whatever space is available so the right side never
    // sits empty.
    const size_t n_cards = list.count > 6 ? 6 : list.count;
    const int rows = (static_cast<int>(n_cards) + kCardCols - 1) / kCardCols;
    const int card_w = (kScreenW - 2 * kSidePad - (kCardCols - 1) * kCardGap)
                       / kCardCols;
    const int card_h = (kCardListH - (rows - 1) * kCardGap) / rows;

    for (size_t i = 0; i < n_cards; ++i) {
        const int col = static_cast<int>(i) % kCardCols;
        const int row = static_cast<int>(i) / kCardCols;
        const int x = kSidePad + col * (card_w + kCardGap);
        const int y = kCardListY + row * (card_h + kCardGap);
        drawCard(g_canvas, list.nodes[i], x, y, card_w, card_h);
    }
    g_canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

void showStatus(const char* msg) {
    g_status.fillCanvas(0);
    g_status.setTextSize(2);
    g_status.setTextDatum(TL_DATUM);
    g_status.drawString(msg, 20, 8);
    g_status.pushCanvas(0, kScreenH - kStatusH, UPDATE_MODE_A2);
}

}  // namespace carl::dashboard
