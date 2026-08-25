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

// Detail page "< Back" hot zone — top-left corner. Drawn as a visible boxed
// label (not an invisible tap zone) so it's discoverable on a device with
// no other UI affordances.
constexpr int kBackW = 140;
constexpr int kBackH = 50;

M5EPD_Canvas g_canvas(&M5.EPD);
M5EPD_Canvas g_status(&M5.EPD);

// Most recent touch-down point, in panel space. Set by consumeTouch() on
// the leading edge of a finger-down event; read via lastTouchPoint().
int g_last_touch_x = 0;
int g_last_touch_y = 0;

// Geometry of one card in the grid, shared between showNodeList() (drawing)
// and hitTestCard() (touch) so they can never drift apart.
struct CardRect { int x, y, w, h; };

CardRect cardRectFor(size_t index, size_t n_cards) {
    const int rows   = (static_cast<int>(n_cards) + kCardCols - 1) / kCardCols;
    const int card_w = (kScreenW - 2 * kSidePad - (kCardCols - 1) * kCardGap) / kCardCols;
    const int card_h = (kCardListH - (rows - 1) * kCardGap) / rows;
    const int col = static_cast<int>(index) % kCardCols;
    const int row = static_cast<int>(index) / kCardCols;
    return CardRect{
        kSidePad + col * (card_w + kCardGap),
        kCardListY + row * (card_h + kCardGap),
        card_w, card_h
    };
}

// "2026-05-04T14:32:00Z" -> "14:32". Empty string if `iso` is malformed —
// callers just get a blank label rather than garbage.
void shortTime(const char* iso, char* out, size_t cap) {
    out[0] = '\0';
    if (iso == nullptr || cap < 6) return;
    const char* t = std::strchr(iso, 'T');
    if (t == nullptr || std::strlen(t) < 6) return;
    std::memcpy(out, t + 1, 5);
    out[5] = '\0';
}

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
    M5.TP.SetRotation(0);    // Must match EPD's rotation or touch coords
                              // land in the panel's native portrait space
                              // instead of our landscape canvas space.
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
        if (edge) {
            tp_finger_t f = M5.TP.readFinger(0);
            g_last_touch_x = f.x;
            g_last_touch_y = f.y;
        }
        return edge;
    }
    return false;
}

void lastTouchPoint(int* x, int* y) {
    if (x) *x = g_last_touch_x;
    if (y) *y = g_last_touch_y;
}

int hitTestCard(const carl::hub::NodeList& list, int touch_x, int touch_y) {
    const size_t n_cards = list.count > 6 ? 6 : list.count;
    for (size_t i = 0; i < n_cards; ++i) {
        const CardRect r = cardRectFor(i, n_cards);
        if (touch_x >= r.x && touch_x < r.x + r.w
            && touch_y >= r.y && touch_y < r.y + r.h) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

bool isBackTouch(int touch_x, int touch_y) {
    return touch_x >= 0 && touch_x < kBackW && touch_y >= 0 && touch_y < kBackH;
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
    // sits empty. Geometry comes from cardRectFor() so hitTestCard() always
    // agrees with what's actually drawn.
    const size_t n_cards = list.count > 6 ? 6 : list.count;
    for (size_t i = 0; i < n_cards; ++i) {
        const CardRect r = cardRectFor(i, n_cards);
        drawCard(g_canvas, list.nodes[i], r.x, r.y, r.w, r.h);
    }
    g_canvas.pushCanvas(0, 0, UPDATE_MODE_GC16);
}

namespace {

// Which reading to trend on the detail-page graph — first present field in
// priority order. Soil first (the plant probe's primary metric); falls back
// through the rest so a room node (no soil) still gets a useful graph.
enum class TrendMetric { kNone, kSoil, kTemperature, kHumidity, kIlluminance };

TrendMetric pickTrendMetric(const carl::hub::History& h) {
    for (size_t i = 0; i < h.count; ++i) if (h.samples[i].has_soil_pct)      return TrendMetric::kSoil;
    for (size_t i = 0; i < h.count; ++i) if (h.samples[i].has_temperature_c) return TrendMetric::kTemperature;
    for (size_t i = 0; i < h.count; ++i) if (h.samples[i].has_humidity_pct)  return TrendMetric::kHumidity;
    for (size_t i = 0; i < h.count; ++i) if (h.samples[i].has_illuminance)   return TrendMetric::kIlluminance;
    return TrendMetric::kNone;
}

bool trendValue(const carl::hub::Reading& r, TrendMetric m, float* out) {
    switch (m) {
        case TrendMetric::kSoil:        if (!r.has_soil_pct)      return false; *out = r.soil_pct;        return true;
        case TrendMetric::kTemperature: if (!r.has_temperature_c) return false; *out = r.temperature_c;    return true;
        case TrendMetric::kHumidity:    if (!r.has_humidity_pct)  return false; *out = r.humidity_pct;     return true;
        case TrendMetric::kIlluminance: if (!r.has_illuminance)   return false; *out = r.illuminance_lux;  return true;
        default: return false;
    }
}

const char* trendLabel(TrendMetric m) {
    switch (m) {
        case TrendMetric::kSoil:        return "Soil %";
        case TrendMetric::kTemperature: return "Temperature (C)";
        case TrendMetric::kHumidity:    return "Humidity %";
        case TrendMetric::kIlluminance: return "Illuminance (lux)";
        default:                        return "";
    }
}

// Simple line-and-dot chart in the (x,y,w,h) box, e-paper style (16-level
// grayscale primitives only, no bitmap libs — same approach as drawPlant()).
// Points are spaced evenly along x by sample index, not by actual elapsed
// time — the hub samples at a roughly steady cadence so this is close
// enough for an at-a-glance trend without the complexity of a time axis.
void drawHistoryGraph(M5EPD_Canvas& c, const carl::hub::History& history,
                      int x, int y, int w, int h) {
    c.drawRect(x, y, w, h, 15);

    const TrendMetric metric = pickTrendMetric(history);

    // static: modest size (~400 B) but this whole file already had one
    // stack-overflow scare from large per-call locals on the Arduino loop
    // task's small default stack (see main.cpp's History comment) — no
    // reason to add back even a little of that risk when static costs
    // nothing (single task, no reentrancy here).
    constexpr size_t kMax = carl::hub::History::kMaxSamples;
    static float ys[kMax];
    static size_t idx[kMax];  // original history index for each plotted point
    size_t n = 0;
    float y_min = 1e9f, y_max = -1e9f;
    for (size_t i = 0; i < history.count && n < kMax; ++i) {
        float v;
        if (!trendValue(history.samples[i], metric, &v)) continue;
        idx[n] = i;
        ys[n] = v;
        if (v < y_min) y_min = v;
        if (v > y_max) y_max = v;
        ++n;
    }

    if (metric == TrendMetric::kNone || n < 2) {
        c.setTextDatum(MC_DATUM);
        c.setTextSize(2);
        c.drawString("Not enough history yet", x + w / 2, y + h / 2);
        return;
    }

    // Fixed 0-100 scale for the two percentage metrics reads more
    // intuitively at a glance than auto-scaling to a tight data range;
    // everything else auto-scales with a little headroom so the line
    // isn't glued to the box edges.
    if (metric == TrendMetric::kSoil || metric == TrendMetric::kHumidity) {
        y_min = 0.0f; y_max = 100.0f;
    } else {
        const float span = y_max - y_min;
        const float pad = (span > 0.01f) ? span * 0.1f : 1.0f;
        y_min -= pad; y_max += pad;
    }

    constexpr int kMarginL = 34;  // room for y-axis min/max labels
    constexpr int kMarginB = 34;  // room for x-axis time labels
    const int plot_x = x + kMarginL;
    const int plot_y = y + 22;    // room for the metric-name label
    const int plot_w = w - kMarginL - 10;
    const int plot_h = h - 22 - kMarginB;
    if (plot_w <= 0 || plot_h <= 0) return;

    auto toPx = [&](size_t i) -> int {
        return plot_x + static_cast<int>((static_cast<float>(i) / static_cast<float>(n - 1)) * plot_w);
    };
    auto toPy = [&](float v) -> int {
        const float t = (v - y_min) / (y_max - y_min);
        return plot_y + plot_h - static_cast<int>(t * plot_h);
    };

    c.drawFastVLine(plot_x, plot_y, plot_h, 10);
    c.drawFastHLine(plot_x, plot_y + plot_h, plot_w, 10);
    for (size_t i = 1; i < n; ++i) {
        c.drawLine(toPx(i - 1), toPy(ys[i - 1]), toPx(i), toPy(ys[i]), 15);
    }
    for (size_t i = 0; i < n; ++i) {
        c.fillCircle(toPx(i), toPy(ys[i]), 3, 15);
    }

    char buf[16];
    c.setTextSize(2);
    c.setTextDatum(TL_DATUM);
    c.drawString(trendLabel(metric), x + 6, y + 4);

    c.setTextDatum(TR_DATUM);
    std::snprintf(buf, sizeof(buf), "%.0f", y_max);
    c.drawString(buf, plot_x - 4, plot_y - 6);
    std::snprintf(buf, sizeof(buf), "%.0f", y_min);
    c.drawString(buf, plot_x - 4, plot_y + plot_h - 10);

    char t0[8], t1[8];
    shortTime(history.samples[idx[0]].ts_iso,     t0, sizeof(t0));
    shortTime(history.samples[idx[n - 1]].ts_iso, t1, sizeof(t1));
    c.setTextDatum(TL_DATUM);
    c.drawString(t0, plot_x, plot_y + plot_h + 8);
    c.setTextDatum(TR_DATUM);
    c.drawString(t1, x + w - 8, plot_y + plot_h + 8);
}

}  // namespace

void showNodeDetail(const carl::hub::Node& node, const carl::hub::History& history) {
    g_canvas.fillCanvas(0);

    // "< Back" — boxed and visible so it reads as tappable; hit zone
    // matches isBackTouch()'s kBackW/kBackH exactly.
    g_canvas.drawRect(4, 4, kBackW - 8, kBackH - 8, 15);
    g_canvas.setTextDatum(TL_DATUM);
    g_canvas.setTextSize(3);
    g_canvas.drawString("< Back", 16, 16);

    // Name + status, to the right of the back button.
    g_canvas.setTextSize(4);
    g_canvas.drawString(node.name, kBackW + 20, 14);
    g_canvas.setTextSize(2);
    g_canvas.setTextDatum(TR_DATUM);
    char status[56];
    std::snprintf(status, sizeof(status), "%s - seen %s",
                  node.online ? "online" : "offline", node.last_seen_iso);
    g_canvas.drawString(status, kScreenW - kSidePad, 22);

    g_canvas.drawFastHLine(kSidePad, kBackH + 4, kScreenW - 2 * kSidePad, 15);

    const int body_y = kBackH + 20;
    char buf[40];

    // Current reading — left column: big primary %, severity, then chips
    // for every other field the node actually reports.
    g_canvas.setTextDatum(TL_DATUM);
    g_canvas.setTextSize(9);
    if (node.latest.has_soil_pct) {
        std::snprintf(buf, sizeof(buf), "%.0f%%", node.latest.soil_pct);
    } else {
        std::snprintf(buf, sizeof(buf), "--");
    }
    g_canvas.drawString(buf, kSidePad, body_y + 10);

    g_canvas.setTextSize(4);
    g_canvas.drawString(severityFor(node.latest), kSidePad, body_y + 110);

    g_canvas.setTextSize(3);
    int cy = body_y + 175;
    if (node.latest.has_temperature_c) {
        std::snprintf(buf, sizeof(buf), "T   %.1f C", node.latest.temperature_c);
        g_canvas.drawString(buf, kSidePad, cy); cy += 36;
    }
    if (node.latest.has_humidity_pct) {
        std::snprintf(buf, sizeof(buf), "H   %.0f %%", node.latest.humidity_pct);
        g_canvas.drawString(buf, kSidePad, cy); cy += 36;
    }
    if (node.latest.has_pressure_hpa) {
        std::snprintf(buf, sizeof(buf), "P   %.0f hPa", node.latest.pressure_hpa);
        g_canvas.drawString(buf, kSidePad, cy); cy += 36;
    }
    if (node.latest.has_illuminance) {
        std::snprintf(buf, sizeof(buf), "Lux %.0f", node.latest.illuminance_lux);
        g_canvas.drawString(buf, kSidePad, cy); cy += 36;
    }
    if (node.has_battery_pct) {
        std::snprintf(buf, sizeof(buf), "Bat %u%%", node.battery_pct);
        g_canvas.drawString(buf, kSidePad, cy); cy += 36;
    }

    // History graph fills the right half of the screen.
    const int graph_x = kScreenW / 2 - 20;
    const int graph_y = body_y;
    const int graph_w = kScreenW - graph_x - kSidePad;
    const int graph_h = kScreenH - graph_y - kSidePad;
    if (history.fetch_ok && history.count >= 2) {
        drawHistoryGraph(g_canvas, history, graph_x, graph_y, graph_w, graph_h);
    } else {
        g_canvas.drawRect(graph_x, graph_y, graph_w, graph_h, 15);
        g_canvas.setTextDatum(MC_DATUM);
        g_canvas.setTextSize(2);
        g_canvas.drawString(history.fetch_ok ? "No history yet" : "History unavailable",
                            graph_x + graph_w / 2, graph_y + graph_h / 2);
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
