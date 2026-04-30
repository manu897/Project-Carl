#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "ui/oled.h"

#include <cstdio>
#include <cstring>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/printk.h>

namespace carl::ui::oled {

namespace {

const struct device* g_disp = DEVICE_DT_GET(DT_ALIAS(carl_oled));

void clearAndPrint(int rows, const char* lines[]) {
    if (g_disp == nullptr) return;
    cfb_framebuffer_clear(g_disp, false);
    for (int i = 0; i < rows; ++i) {
        if (lines[i] != nullptr) cfb_print(g_disp, lines[i], 0, i);
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

}  // namespace

bool init() {
    if (g_disp == nullptr || !device_is_ready(g_disp)) return false;
    if (display_set_pixel_format(g_disp, PIXEL_FORMAT_MONO10) != 0) return false;
    if (cfb_framebuffer_init(g_disp) != 0) return false;
    cfb_framebuffer_set_font(g_disp, 0);
    display_blanking_off(g_disp);
    return true;
}

void showWelcome(const char* key_hex_or_null) {
    char l1[24] = "Project Carl";
    char l2[24] = "Plant Monitor";
    char l3[40];
    char l4[40];
    if (key_hex_or_null != nullptr) {
        std::snprintf(l3, sizeof(l3), "KEY %.16s", key_hex_or_null);
        std::snprintf(l4, sizeof(l4), "    %s", key_hex_or_null + 16);
    } else {
        std::snprintf(l3, sizeof(l3), "Provisioned");
        l4[0] = '\0';
    }
    const char* lines[4] = { l1, l2, l3, l4[0] ? l4 : nullptr };
    clearAndPrint(4, lines);
}

void showReadings(const carl::sensors::Sample& s, carl::thresholds::Severity sev) {
    char l1[32], l2[32], l3[32], l4[32];
    std::snprintf(l1, sizeof(l1), "T %4.1fC  H %4.1f%%",
                  s.bme_ok ? static_cast<double>(s.temperature_c) : 0.0,
                  s.bme_ok ? static_cast<double>(s.humidity_pct) : 0.0);
    std::snprintf(l2, sizeof(l2), "P %6.1f hPa",
                  s.bme_ok ? static_cast<double>(s.pressure_hpa) : 0.0);
    std::snprintf(l3, sizeof(l3), "Soil %3.0f%% [%s]",
                  s.soil_ok ? static_cast<double>(s.soil_pct) : 0.0,
                  severityLabel(sev));
    std::snprintf(l4, sizeof(l4), "Lux  %5.0f",
                  s.veml_ok ? static_cast<double>(s.illuminance_lux) : 0.0);
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

}  // namespace carl::ui::oled

#endif  // CONFIG_CARL_DISPLAY_PROFILE
