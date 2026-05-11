#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "ui/oled.h"

#include <cstdio>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

#ifdef CONFIG_CARL_HAS_QR
#include "qrcodegen.h"   // Nayuki — vendored at firmware/common/qr/ (already
                          // in the include path via firmware/common in
                          // CMakeLists target_include_directories).
#endif

namespace carl::ui::oled {

namespace {

const struct device* g_disp = DEVICE_DT_GET(DT_ALIAS(carl_oled));

// Carl brand-mark leaf — 32×32 monochrome bitmap, **tilted** so the tip
// points toward the upper-left and the body curves down-right, matching the
// iOS app icon's leaf orientation. Each entry is one row, bit n = column n
// (LSB = leftmost pixel). Refine the silhouette when the iOS icon design is
// finalized — see ios_app_roadmap.md.
constexpr uint32_t kLeafBitmap[32] = {
    0x00000300U, 0x00000700U, 0x00000F80U, 0x00001F80U,  // tip @ upper-left
    0x00003F80U, 0x00007FC0U, 0x0000FFC0U, 0x0001FFC0U,
    0x0003FFC0U, 0x0007FF80U, 0x000FFF80U, 0x001FFF00U,
    0x003FFF00U, 0x007FFE00U, 0x007FFE00U, 0x007FFC00U,
    0x007FF800U, 0x003FF000U, 0x001FE000U, 0x000F8000U,
    0x000F0000U, 0x000C0000U, 0x000C0000U, 0x000C0000U,  // base curving right
    0x000C0000U, 0x00040000U, 0x00040000U, 0x00040000U,  // stem
    0x00040000U, 0x00040000U, 0x00000000U, 0x00000000U,
};

// Convert a 32×32 row-major bitmap to SSD1306 page format and blit at (x, y).
// y must be aligned to a page boundary (multiple of 8). For our welcome the
// leaf sits at (48, 0) — top half of the screen, horizontally centered.
void drawLeafBitmap(uint16_t x, uint16_t y) {
    if (g_disp == nullptr) return;
    static constexpr int kW = 32;
    static constexpr int kH = 32;
    static constexpr int kPages = kH / 8;
    uint8_t buf[kW * kPages] = {};
    for (int row = 0; row < kH; ++row) {
        const uint32_t bits = kLeafBitmap[row];
        const int page = row / 8;
        const int bit_in_page = row % 8;
        for (int col = 0; col < kW; ++col) {
            if (bits & (1U << col)) {
                buf[page * kW + col] |= static_cast<uint8_t>(1U << bit_in_page);
            }
        }
    }
    struct display_buffer_descriptor desc{};
    desc.buf_size = sizeof(buf);
    desc.width    = kW;
    desc.height   = kH;
    desc.pitch    = kW;
    display_write(g_disp, x, y, &desc, buf);
}

void clearAndPrint(int rows, const char* lines[]) {
    if (g_disp == nullptr) return;
    cfb_framebuffer_clear(g_disp, false);

    // cfb_print() takes y in PIXELS, not row index. Look up the font's
    // actual height once and stride by it so lines don't overlap.
    uint8_t fw = 0, fh = 0;
    cfb_get_font_size(g_disp, 0, &fw, &fh);
    if (fh == 0) fh = 10;  // sensible fallback if the query fails

    for (int i = 0; i < rows; ++i) {
        if (lines[i] != nullptr && lines[i][0] != '\0') {
            cfb_print(g_disp, lines[i], 0, i * fh);
        }
    }
    cfb_framebuffer_finalize(g_disp);
}

const char* severityLabel(carl::thresholds::Severity s) {
    switch (s) {
        case carl::thresholds::Severity::kOk:       return "OK";
        case carl::thresholds::Severity::kWarning:  return "DRY";
        case carl::thresholds::Severity::kCritical: return "!!!";
    }
    return "?";
}

// The default Zephyr CFB font on this build is font10x16 (10 px wide × 16 px
// tall). On a 128×64 OLED that gives 12 chars × 4 lines. Strings here are
// kept ≤12 chars so nothing clips off the right edge.

}  // namespace

bool init() {
    if (g_disp == nullptr || !device_is_ready(g_disp)) return false;
    if (display_set_pixel_format(g_disp, PIXEL_FORMAT_MONO10) != 0) return false;
    if (cfb_framebuffer_init(g_disp) != 0) return false;
    cfb_framebuffer_set_font(g_disp, 0);
    display_blanking_off(g_disp);
    return true;
}

void wake() {
    if (g_disp != nullptr) display_blanking_off(g_disp);
}

void sleep() {
    if (g_disp != nullptr) display_blanking_on(g_disp);
}

void showLeafSplash() {
    if (g_disp == nullptr) return;

    // 1) CFB pass: clear the screen and print the brand text in the bottom
    //    half. cfb_framebuffer_finalize() transfers CFB's buffer to the
    //    panel, including the unwritten top half (which is blank).
    cfb_framebuffer_clear(g_disp, false);
    uint8_t fw = 0, fh = 0;
    cfb_get_font_size(g_disp, 0, &fw, &fh);
    if (fh == 0) fh = 16;
    // "Project Carl" centered on row 2 (y=32). 12 chars × 10 px = 120 px,
    // 4 px left padding centers it in the 128 px width. The leaf bitmap
    // above carries the rest of the brand vibe — no tagline needed.
    cfb_print(g_disp, "Project Carl", 4, 2 * fh);
    cfb_framebuffer_finalize(g_disp);

    // 2) Direct display write of the leaf bitmap at (48, 0). This overwrites
    //    the top-half region of the panel directly — CFB's buffer doesn't
    //    track it, but that's fine because the next showReadings() call
    //    clears CFB and the leaf gets washed out cleanly.
    drawLeafBitmap(48, 0);
}

void showWelcome(const char* key_hex_or_null) {
    // 4 rows × 12 chars budget on the SSD1306 with font10x16.
    // First-boot path needs to show all 32 chars of the AES key so the
    // user can transcribe it into the hub. We drop the "Plant Monitor"
    // tagline (covered by the leaf splash before this screen) to free up
    // three rows for the key — 12 + 12 + 8 = 32.
    char l1[16] = "Project Carl";
    char l2[16] = {};
    char l3[16] = {};
    char l4[16] = {};
    if (key_hex_or_null != nullptr) {
        std::snprintf(l2, sizeof(l2), "%.12s", key_hex_or_null);       // chars 0..11
        std::snprintf(l3, sizeof(l3), "%.12s", key_hex_or_null + 12);  // chars 12..23
        std::snprintf(l4, sizeof(l4), "%.8s",  key_hex_or_null + 24);  // chars 24..31
    } else {
        std::snprintf(l2, sizeof(l2), "Provisioned");
    }
    const char* lines[4] = { l1, l2, l3[0] ? l3 : nullptr, l4[0] ? l4 : nullptr };
    clearAndPrint(4, lines);
}

void showReadings(const carl::sensors::Sample& s, carl::thresholds::Severity sev,
                  bool live) {
    char l1[16], l2[16], l3[16], l4[16];
    // Row 1: temperature (and humidity if both available).
    if (s.temp_ok && s.humidity_ok) {
        std::snprintf(l1, sizeof(l1), "T%4.1f H%2.0f%%",
                      static_cast<double>(s.temperature_c),
                      static_cast<double>(s.humidity_pct));
    } else if (s.temp_ok) {
        std::snprintf(l1, sizeof(l1), "T %4.1f C",
                      static_cast<double>(s.temperature_c));
    } else {
        std::snprintf(l1, sizeof(l1), "Temp --");
    }
    // Row 2: pressure (hidden if unavailable).
    if (s.pressure_ok) {
        std::snprintf(l2, sizeof(l2), "P %6.1fhPa",
                      static_cast<double>(s.pressure_hpa));
    } else {
        l2[0] = '\0';
    }
    // Row 3: soil moisture + severity, or a friendly nudge to calibrate.
    if (carl::sensors::hasSoilCalibration() && s.soil_ok) {
        std::snprintf(l3, sizeof(l3), "Soil %3.0f%% %s",
                      static_cast<double>(s.soil_pct),
                      severityLabel(sev));
    } else if (s.soil_ok) {
        std::snprintf(l3, sizeof(l3), "Calibrate");
    } else {
        std::snprintf(l3, sizeof(l3), "No probe");
    }
    // Row 4: illuminance only. Hidden when no VEML7700 is present —
    // previously fell back to the raw soil ADC count for bring-up, but
    // that's a developer view that doesn't belong on the customer screen.
    if (s.veml_ok) {
        std::snprintf(l4, sizeof(l4), "Lux %5.0f",
                      static_cast<double>(s.illuminance_lux));
    } else {
        l4[0] = '\0';
    }
    const char* lines[4] = {
        l1,
        l2[0] ? l2 : nullptr,
        l3,
        l4[0] ? l4 : nullptr,
    };
    clearAndPrint(4, lines);

    // Draw a small "L" in the top-right corner when live mode is active.
    // This sits on the same row as l1 but at column 11 (last col, ~x=110px),
    // so it doesn't overlap the typical 8-char l1 strings ("Temp --").
    if (live && g_disp != nullptr) {
        // Re-render: cfb has already been finalized above. We need a minimal
        // "draw L without clearing" path. Quickest: re-clear and redraw.
        cfb_framebuffer_clear(g_disp, false);
        uint8_t fw = 0, fh = 0;
        cfb_get_font_size(g_disp, 0, &fw, &fh);
        if (fh == 0) fh = 10;
        for (int i = 0; i < 4; ++i) {
            if (lines[i] != nullptr && lines[i][0] != '\0') {
                cfb_print(g_disp, lines[i], 0, i * fh);
            }
        }
        cfb_print(g_disp, "L", 128 - fw, 0);
        cfb_framebuffer_finalize(g_disp);
    }
}

void showDebugReadings(const carl::sensors::Sample& s,
                       carl::thresholds::Severity sev,
                       uint32_t sample_no) {
    // 12 chars × 4 rows budget. Optimise for raw values + calibration so
    // the dev can spot probe / threshold issues without serial output.
    char l1[16], l2[16], l3[16], l4[16];

    // Row 1: raw soil ADC + computed percent.
    if (s.soil_ok) {
        std::snprintf(l1, sizeof(l1), "ADC %4u %3.0f%%",
                      s.soil_raw_adc,
                      static_cast<double>(s.soil_pct));
    } else {
        std::snprintf(l1, sizeof(l1), "ADC ----");
    }
    // Row 2: dry / wet calibration values (hyphen if not yet calibrated).
    if (carl::sensors::hasSoilCalibration()) {
        std::snprintf(l2, sizeof(l2), "D%4u W%4u",
                      carl::sensors::soilDryRaw(),
                      carl::sensors::soilWetRaw());
    } else {
        std::snprintf(l2, sizeof(l2), "Dry- Wet-");
    }
    // Row 3: severity + rolling sample counter (low 4 digits to fit).
    std::snprintf(l3, sizeof(l3), "Sev %s s%04u",
                  severityLabel(sev), sample_no & 0xFFFF);
    // Row 4: temperature (compact, 4 chars) + lux raw (or dashes).
    if (s.temp_ok) {
        std::snprintf(l4, sizeof(l4), "T %4.1fC L%5.0f",
                      static_cast<double>(s.temperature_c),
                      s.veml_ok ? static_cast<double>(s.illuminance_lux) : 0.0);
    } else {
        std::snprintf(l4, sizeof(l4), "T --   L%5.0f",
                      s.veml_ok ? static_cast<double>(s.illuminance_lux) : 0.0);
    }
    const char* lines[4] = { l1, l2, l3, l4 };
    clearAndPrint(4, lines);
}

void showCalibrationStep(const char* title, const char* hint) {
    const char* lines[4] = { "Calibration", title, hint, nullptr };
    clearAndPrint(4, lines);
}

void showError(const char* msg) {
    const char* lines[4] = { "ERROR", msg, nullptr, nullptr };
    clearAndPrint(4, lines);
}

#ifdef CONFIG_CARL_HAS_QR

namespace {

// Format the Carl provisioning URL into out (>= 64 bytes). Output is
// pure-alphanumeric so the QR encoder can use V3 alphanumeric mode and we
// can render at 2 px / module = 58×58 (vs V5 byte mode = 37×37 at 1 px).
//
// Example output:
//   CARL://AABBCCDDEEFF/4B1F9C8A3E2D6F70B15C4D8A9E3F2C10
//
// Schema:
//   * scheme: CARL (uppercase; URL schemes are case-insensitive per RFC
//     3986 so iOS still routes it to the carl-registered handler).
//   * host:   MAC as 12 uppercase hex chars, no colons.
//   * path:   /<32 uppercase hex key>
//
// 7 (scheme) + 12 (mac) + 1 (slash) + 32 (key) = 52 chars total.
void formatProvisioningUrl(const uint8_t mac6[6], const uint8_t key16[16],
                           char* out, size_t out_len) {
    static const char* kHexUpper = "0123456789ABCDEF";
    std::snprintf(out, out_len,
        "CARL://%c%c%c%c%c%c%c%c%c%c%c%c/",
        kHexUpper[mac6[5] >> 4], kHexUpper[mac6[5] & 0x0F],
        kHexUpper[mac6[4] >> 4], kHexUpper[mac6[4] & 0x0F],
        kHexUpper[mac6[3] >> 4], kHexUpper[mac6[3] & 0x0F],
        kHexUpper[mac6[2] >> 4], kHexUpper[mac6[2] & 0x0F],
        kHexUpper[mac6[1] >> 4], kHexUpper[mac6[1] & 0x0F],
        kHexUpper[mac6[0] >> 4], kHexUpper[mac6[0] & 0x0F]);
    size_t pos = std::strlen(out);
    for (int i = 0; i < 16 && pos + 2 < out_len; ++i) {
        out[pos++] = kHexUpper[key16[i] >> 4];
        out[pos++] = kHexUpper[key16[i] & 0x0F];
    }
    out[pos] = '\0';
}

}  // namespace
#endif  // CONFIG_CARL_HAS_QR

bool showProvisioningQR(const uint8_t mac6[6], const uint8_t key16[16]) {
    if (g_disp == nullptr) return false;

#ifndef CONFIG_CARL_HAS_QR
    // QR support disabled at build time. Show a stub message so the welcome
    // flow doesn't silently skip — the user can still provision by reading
    // the AES key off the previous welcome screen.
    (void)mac6; (void)key16;
    const char* lines[4] = {
        "QR off",
        "Enable",
        "CARL_HAS_QR",
        "see README",
    };
    clearAndPrint(4, lines);
    return false;
#else
    char url[64];
    formatProvisioningUrl(mac6, key16, url, sizeof(url));

    // The URL is 52 alphanumeric chars; qrcodegen autodetects alphanumeric
    // mode. Force exactly Version 3 so the result is always 29×29 modules,
    // which we render at 2 px / module = 58×58 px to fill the OLED nicely.
    // boostEcl=true upgrades L → M error correction since 52 chars fits M
    // (61 cap), giving us a more scan-tolerant code at no size cost.
    static constexpr int kVersion = 3;
    uint8_t qrcode[qrcodegen_BUFFER_LEN_FOR_VERSION(kVersion)];
    uint8_t tempbuf[qrcodegen_BUFFER_LEN_FOR_VERSION(kVersion)];

    const bool ok = qrcodegen_encodeText(
        url, tempbuf, qrcode,
        qrcodegen_Ecc_LOW,
        /*minVersion=*/kVersion, /*maxVersion=*/kVersion,
        qrcodegen_Mask_AUTO,
        /*boostEcl=*/true);
    if (!ok) {
        const char* lines[4] = { "QR encode", "failed", nullptr, nullptr };
        clearAndPrint(4, lines);
        return false;
    }

    const int qr_modules = qrcodegen_getSize(qrcode);  // 29 for V3
    static constexpr int kScale = 2;                    // px per module
    const int qr_px = qr_modules * kScale;              // 58
    const int x_off = (128 - qr_px) / 2;                // 35
    const int y_off = (64  - qr_px) / 2;                // 3

    // Render the QR onto a full-screen buffer with a bright (1) background
    // and dark (0) modules. Phone camera reads bright OLED pixels as "white"
    // (no-module / quiet zone) and dark pixels as "black" (module) — that's
    // the standard polarity QR scanners expect. Doing the entire 128×64 in
    // one display_write guarantees the 4-module quiet zone around the QR is
    // present on all four sides without a stripe of stale content showing
    // through.
    static constexpr int kScreenW = 128;
    static constexpr int kScreenH = 64;
    static constexpr int kPages   = kScreenH / 8;
    static uint8_t page_buf[kScreenW * kPages];        // 1024 bytes — static
                                                        // so it doesn't sit on
                                                        // main's 2 KB stack.
    std::memset(page_buf, 0xFF, sizeof(page_buf));     // all lit (bright bg)

    for (int qy = 0; qy < qr_modules; ++qy) {
        for (int qx = 0; qx < qr_modules; ++qx) {
            if (!qrcodegen_getModule(qrcode, qx, qy)) continue;
            // Each module expands to a kScale × kScale block of dark pixels.
            for (int dy = 0; dy < kScale; ++dy) {
                const int row  = qy * kScale + dy + y_off;
                const int page = row / 8;
                const int bit  = row % 8;
                for (int dx = 0; dx < kScale; ++dx) {
                    const int col = qx * kScale + dx + x_off;
                    page_buf[page * kScreenW + col] &=
                        static_cast<uint8_t>(~(1U << bit));
                }
            }
        }
    }

    struct display_buffer_descriptor desc{};
    desc.buf_size = sizeof(page_buf);
    desc.width    = kScreenW;
    desc.height   = kScreenH;
    desc.pitch    = kScreenW;

    display_write(g_disp, 0, 0, &desc, page_buf);
    return true;
#endif  // CONFIG_CARL_HAS_QR
}

}  // namespace carl::ui::oled

#endif  // CONFIG_CARL_DISPLAY_PROFILE
