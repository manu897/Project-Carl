#include "thresholds.h"

#include <cstring>

#include <zephyr/settings/settings.h>

namespace carl::thresholds {

namespace {

Config g_cfg = { /*warn_pct=*/30, /*crit_pct=*/15 };

// Own subtree (see keystore.cpp comment) so Zephyr's settings routing
// doesn't collide with the keystore + cal handlers.
constexpr const char* kPath = "carl_th/cfg";

int settingsCb(const char* name, size_t len, settings_read_cb read_cb, void* cb_arg) {
    if (std::strcmp(name, "cfg") == 0 && len == sizeof(g_cfg)) {
        read_cb(cb_arg, &g_cfg, sizeof(g_cfg));
    }
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(carl_thresholds, "carl_th",
                               nullptr, settingsCb, nullptr, nullptr);

}  // namespace

bool init() { return true; }

Severity evaluate(float pct) {
    if (pct < static_cast<float>(g_cfg.crit_pct)) return Severity::kCritical;
    if (pct < static_cast<float>(g_cfg.warn_pct)) return Severity::kWarning;
    return Severity::kOk;
}

const Config& config() { return g_cfg; }

void setConfig(Config c) {
    g_cfg = c;
    settings_save_one(kPath, &g_cfg, sizeof(g_cfg));
}

}  // namespace carl::thresholds
