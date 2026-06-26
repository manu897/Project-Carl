#include "calibration.h"

#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

#include "sensors.h"
#include "ui/button.h"
#include "ui/oled.h"

namespace carl::calibration {

namespace {

// Wait up to `timeout_ms` for the user to short-press the button.
// Returns true on press, false on timeout.
bool waitForPress(int timeout_ms) {
    const int64_t deadline = k_uptime_get() + timeout_ms;
    while (k_uptime_get() < deadline) {
        if (carl::ui::button::consumeShortPress()) return true;
        k_msleep(20);
    }
    return false;
}

}  // namespace

void runDisplayFlow() {
    carl::ui::oled::showCalibrationStep("DRY soil", "Place probe, press btn");
    if (!waitForPress(60000)) {
        carl::ui::oled::showCalibrationStep("Timeout", "Skipping calibration");
        k_msleep(2000);
        return;
    }
    uint16_t dry = 0;
    if (!carl::sensors::readSoilAveraged(&dry)) {
        carl::ui::oled::showCalibrationStep("Dry FAIL", "ADC error");
        k_msleep(2000);
        return;
    }

    carl::ui::oled::showCalibrationStep("WET soil", "Place probe, press btn");
    if (!waitForPress(60000)) return;
    uint16_t wet = 0;
    if (!carl::sensors::readSoilAveraged(&wet)) return;

    if (dry == wet) {
        carl::ui::oled::showCalibrationStep("Cal FAIL", "dry == wet");
        k_msleep(2000);
        return;
    }
    carl::sensors::setSoilCalibration(dry, wet);
    carl::ui::oled::showCalibrationStep("Saved!", "Resuming");
    k_msleep(1500);
}

}  // namespace carl::calibration

#endif  // CONFIG_CARL_DISPLAY_PROFILE
