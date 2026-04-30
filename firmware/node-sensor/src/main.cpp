// Project-Carl — XIAO nRF52840 sensor node entry point.
//
// One firmware, two profiles. The display profile pulls in the OLED + button
// + buzzer + calibration paths; the cheap profile leaves them as no-ops via
// CONFIG_CARL_DISPLAY_PROFILE.

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

#include "bthome_emit.h"
#include "keystore.h"
#include "sensors.h"
#include "thresholds.h"

#ifdef CONFIG_CARL_DISPLAY_PROFILE
#include "calibration.h"
#include "ui/button.h"
#include "ui/buzzer.h"
#include "ui/oled.h"
#endif

namespace {

void boot() {
    settings_subsys_init();
    settings_load();

    if (!carl::sensors::init())   printk("sensors_init failed\n");
    if (!carl::thresholds::init()) printk("thresholds_init failed\n");
    if (!carl::keystore::init())   printk("keystore_init failed\n");

    if (bt_enable(nullptr) != 0) printk("bt_enable failed\n");
    carl::bthome_emit::init();

#ifdef CONFIG_CARL_DISPLAY_PROFILE
    carl::ui::oled::init();
    carl::ui::button::init();
    carl::ui::buzzer::init();

    char hex[33];
    carl::keystore::keyHex(hex);
    carl::ui::oled::showWelcome(carl::keystore::isFreshKey() ? hex : nullptr);
    if (carl::keystore::isFreshKey()) {
        printk("AES key (transcribe to hub): %s\n", hex);
    }

    // Boot-time button hold → calibration flow.
    if (carl::ui::button::isHeldNow()) {
        k_msleep(500);  // confirm hold
        if (carl::ui::button::isHeldNow()) {
            carl::calibration::runDisplayFlow();
        }
    }

    if (!carl::sensors::hasSoilCalibration()) {
        carl::ui::oled::showCalibrationStep("No cal", "Hold btn at boot");
        k_sleep(K_SECONDS(2));
    }
#endif
}

}  // namespace

int main(void) {
    boot();

    while (true) {
        carl::sensors::Sample s{};
        carl::sensors::sample(&s);
        const auto severity = carl::thresholds::evaluate(s.soil_pct);

#ifdef CONFIG_CARL_DISPLAY_PROFILE
        carl::ui::oled::showReadings(s, severity);
        carl::ui::buzzer::alert(severity);
#endif

        carl::bthome_emit::broadcastOnce(s, severity);

        k_sleep(K_SECONDS(CONFIG_CARL_SAMPLE_INTERVAL_SEC));
    }
    return 0;
}
