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
#include "dev_cal.h"
#include "keystore.h"
#include "sensors.h"
#include "thresholds.h"

#ifdef CONFIG_CARL_DISPLAY_PROFILE
#include "calibration.h"
#include "ui/button.h"
#include "ui/oled.h"
#endif
#ifdef CONFIG_CARL_HAS_BUZZER
#include "ui/buzzer.h"
#endif
#ifdef CONFIG_CARL_HAS_RGB
#include "ui/rgb.h"
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

    // Surface the provisioning credentials on the serial console as a single
    // machine-parseable line so tools/provision.py can capture them and print
    // a stick-on QR label. The budget/cheap node has no screen, so this (or
    // the printed label) is how its key reaches the hub. On the display
    // profile we only emit it on first boot (the OLED QR is the primary
    // path); the cheap profile has no other surface, so emit every boot —
    // harmless in the field where no serial console is attached.
    {
        bt_addr_le_t addr{};
        size_t addr_count = 1;
        bt_id_get(&addr, &addr_count);
#ifdef CONFIG_CARL_DISPLAY_PROFILE
        const bool emit_prov = carl::keystore::isFreshKey();
#else
        const bool emit_prov = true;
#endif
        if (addr_count == 1 && emit_prov) {
            char hex[33];
            carl::keystore::keyHex(hex);
            const uint8_t *m = addr.a.val;
            printk("CARL-PROV mac=%02X:%02X:%02X:%02X:%02X:%02X key=%s\n",
                   m[5], m[4], m[3], m[2], m[1], m[0], hex);
        }
    }

    // Dev-mode serial calibration listener. No-op unless CONFIG_CARL_DEV_CAL —
    // gives the buttonless node a CAL DRY/WET/SAVE/SHOW path over the console.
    carl::dev_cal::start();

    // Local feedback (RGB + buzzer) — both profiles. This is the screenless
    // budget node's only status surface, so it lives outside the display
    // block. The display profile plays its welcome chime during the splash
    // sequence below, so we only chime here on the cheap profile.
#ifdef CONFIG_CARL_HAS_RGB
    carl::ui::rgb::init();
    carl::ui::rgb::bootFlash();
#endif
#ifdef CONFIG_CARL_HAS_BUZZER
    carl::ui::buzzer::init();
#ifndef CONFIG_CARL_DISPLAY_PROFILE
    carl::ui::buzzer::welcomeChime();
#endif
#endif

#ifdef CONFIG_CARL_DISPLAY_PROFILE
    carl::ui::oled::init();
    carl::ui::button::init();

    // Welcome sequence — paced so the user actually has time to see it:
    //   1. Leaf splash with brand text shown immediately.
    //   2. Nokia tune (~3 s) plays while the splash stays on screen.
    //   3. Splash continues for a beat after the tune so the screen
    //      doesn't whip away the moment the music ends.
    //   4. On first boot only, switch to the key transcription screen
    //      and hold it long enough to read / photograph (5 s).
    carl::ui::oled::showLeafSplash();
#ifdef CONFIG_CARL_HAS_BUZZER
    carl::ui::buzzer::welcomeChime();
#endif
    k_sleep(K_MSEC(800));

    char hex[33];
    carl::keystore::keyHex(hex);
    carl::ui::oled::showWelcome(carl::keystore::isFreshKey() ? hex : nullptr);
    if (carl::keystore::isFreshKey()) {
        printk("AES key (transcribe to hub): %s\n", hex);
        k_sleep(K_SECONDS(5));   // give the user time to capture the key

        // Then show the same key as a scannable QR for the iOS app's
        // Add-Plant flow. Held for 15 s so the user has plenty of time
        // to point a phone at the OLED.
        bt_addr_le_t addr{};
        size_t addr_count = 1;
        bt_id_get(&addr, &addr_count);
        if (addr_count == 1) {
            carl::ui::oled::showProvisioningQR(addr.a.val, carl::keystore::key());
            k_sleep(K_SECONDS(15));
        }
    } else {
        k_sleep(K_SECONDS(2));   // shorter hold on subsequent boots
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

    // Discard any latched short-press from boot-time button hold so we don't
    // immediately enter live mode on the first loop iteration.
    (void)carl::ui::button::consumeShortPress();
#endif
}

}  // namespace

namespace {

// Pick the next sample interval. In debug mode this is a fixed fast cadence
// (CARL_DEBUG_SAMPLE_INTERVAL_SEC, default 1 s). In customer mode it's
// adaptive — critical → fast, healthy → slow — to maximise battery life.
int sampleIntervalSecForSeverity(carl::thresholds::Severity sev) {
#ifdef CONFIG_CARL_DEBUG_MODE
    (void)sev;
    return CONFIG_CARL_DEBUG_SAMPLE_INTERVAL_SEC;
#else
    switch (sev) {
        case carl::thresholds::Severity::kCritical:
            return CONFIG_CARL_SAMPLE_INTERVAL_CRITICAL_SEC;
        case carl::thresholds::Severity::kWarning:
            return CONFIG_CARL_SAMPLE_INTERVAL_WARNING_SEC;
        case carl::thresholds::Severity::kOk:
        default:
            return CONFIG_CARL_SAMPLE_INTERVAL_NORMAL_SEC;
    }
#endif
}

}  // namespace

int main(void) {
    boot();

    // Adaptive cadence + power-aware display:
    //   - Sample interval depends on the last severity (1 min critical →
    //     5 min warning → 30 min normal). Healthy plants barely wake the MCU.
    //   - OLED is asleep most of the time. It's only awake during:
    //       a) the boot grace window (so the user reads the welcome + key + QR)
    //       b) for CARL_DISPLAY_WAKE_SEC after a short button press
    //       c) for as long as severity is critical, plus 30 s after it clears
    //   - Buzzer fires once per critical sample (rate-limited by sample interval).

    // last_sample_ms used to be INT64_MIN as a "force first sample" sentinel,
    // but `(now - INT64_MIN)` is signed-overflow UB and on this target it
    // landed negative, so sample_due stayed false until a button press
    // short-circuited the OR. That left the welcome splash on screen and
    // the radio silent until the user poked USR — exactly the symptom we
    // hit. Use an explicit first-shot flag instead; safe for any interval.
    int64_t last_sample_ms       = 0;
    bool    force_first_sample   = true;
    int64_t display_wake_until   = 0;
    auto    prev_severity        = carl::thresholds::Severity::kOk;
    bool    display_is_on        = true;   // boot leaves display on
    uint32_t sample_no           = 0;       // rolling counter for debug view

#ifdef CONFIG_CARL_DISPLAY_PROFILE
#ifdef CONFIG_CARL_DEBUG_MODE
    // Debug mode pins the display awake forever. Customer mode gives a
    // grace window then sleeps it.
    display_wake_until = INT64_MAX;
#else
    display_wake_until = k_uptime_get()
                         + CONFIG_CARL_DISPLAY_BOOT_GRACE_SEC * 1000;
#endif
#endif

    while (true) {
#ifdef CONFIG_CARL_DISPLAY_PROFILE
        // Long press: re-show the provisioning QR for 30 s. Display wakes
        // for the duration regardless of timer state.
        if (carl::ui::button::consumeLongPress()) {
            bt_addr_le_t addr{};
            size_t addr_count = 1;
            bt_id_get(&addr, &addr_count);
            if (addr_count == 1) {
                if (!display_is_on) { carl::ui::oled::wake(); display_is_on = true; }
                carl::ui::oled::showProvisioningQR(addr.a.val,
                                                   carl::keystore::key());
                k_sleep(K_SECONDS(30));
                display_wake_until = k_uptime_get()
                                     + CONFIG_CARL_DISPLAY_WAKE_SEC * 1000;
            }
        }

        // Short press: wake the display + force an immediate sample so the
        // user sees fresh data instead of whatever was rendered last.
        const bool short_pressed = carl::ui::button::consumeShortPress();
        if (short_pressed) {
            display_wake_until = k_uptime_get()
                                 + CONFIG_CARL_DISPLAY_WAKE_SEC * 1000;
            force_first_sample = true;   // re-use the first-sample path
        }
#else
        const bool short_pressed = false;
#endif

        const int interval_sec = sampleIntervalSecForSeverity(prev_severity);
        const int64_t now = k_uptime_get();
        const bool sample_due =
            force_first_sample
            || (now - last_sample_ms) >= interval_sec * 1000
            || short_pressed;

        if (sample_due) {
            carl::sensors::Sample s{};
            carl::sensors::sample(&s);

            // Until the soil probe is calibrated, treat severity as OK so
            // an uncalibrated floating ADC reading doesn't pin everything
            // to critical and burn battery on a 1-min cadence.
            const bool soil_actionable =
                s.soil_ok && carl::sensors::hasSoilCalibration();
            const auto severity = soil_actionable
                ? carl::thresholds::evaluate(s.soil_pct)
                : carl::thresholds::Severity::kOk;

#ifdef CONFIG_CARL_DISPLAY_PROFILE
            using Sev = carl::thresholds::Severity;
#ifndef CONFIG_CARL_DEBUG_MODE
            // Customer mode: critical wakes (or keeps awake) the display.
            // As soon as it clears, leave the display on for one wake
            // window so the user sees the recovery, then let it sleep.
            if (severity == Sev::kCritical) {
                display_wake_until = INT64_MAX;
            } else if (prev_severity == Sev::kCritical && severity != Sev::kCritical) {
                display_wake_until = now + CONFIG_CARL_DISPLAY_WAKE_SEC * 1000;
            }
#endif

            const bool should_show = now < display_wake_until;
            if (should_show && !display_is_on) {
                carl::ui::oled::wake();
                display_is_on = true;
            }
            if (display_is_on) {
#ifdef CONFIG_CARL_DEBUG_MODE
                carl::ui::oled::showDebugReadings(s, severity, sample_no);
#else
                carl::ui::oled::showReadings(s, severity, /*live=*/false);
#endif
            }

#endif

            // Local feedback on both profiles: a brief RGB blink conveys
            // severity then the LED goes dark (never solid-on — protects the
            // battery, same power-aware idea as the OLED). The buzzer beeps on
            // critical (rate-limited by the 1-min critical cadence — one beep
            // per minute, not per loop). Debug mode keeps the buzzer silent so
            // it doesn't beep continuously while iterating.
#ifdef CONFIG_CARL_HAS_RGB
            carl::ui::rgb::blinkSeverity(severity);
#endif
#if defined(CONFIG_CARL_HAS_BUZZER) && !defined(CONFIG_CARL_DEBUG_MODE)
            if (severity == carl::thresholds::Severity::kCritical) {
                carl::ui::buzzer::alert(severity);
            }
#endif

            carl::bthome_emit::broadcastOnce(s, severity);

            last_sample_ms     = now;
            prev_severity      = severity;
            force_first_sample = false;
            ++sample_no;
        }

#if defined(CONFIG_CARL_DISPLAY_PROFILE) && !defined(CONFIG_CARL_DEBUG_MODE)
        // If the wake window has elapsed and we're not in a critical state,
        // put the display to sleep to drop current draw by ~10 mA. Debug
        // mode keeps the panel awake forever so this branch is compiled out.
        if (display_is_on
            && k_uptime_get() >= display_wake_until
            && prev_severity != carl::thresholds::Severity::kCritical) {
            carl::ui::oled::sleep();
            display_is_on = false;
        }
#endif

        // Sleep until the next event: either the next sample, or — if the
        // display is awake — the next OLED refresh, whichever is sooner.
        const int64_t now2 = k_uptime_get();
        int64_t until_next_sample_ms =
            last_sample_ms + interval_sec * 1000 - now2;
        if (until_next_sample_ms < 100) until_next_sample_ms = 100;
        int64_t sleep_ms = until_next_sample_ms;
#ifdef CONFIG_CARL_DISPLAY_PROFILE
        if (display_is_on) {
            const int64_t refresh_ms = CONFIG_CARL_OLED_REFRESH_SEC * 1000;
            if (refresh_ms < sleep_ms) sleep_ms = refresh_ms;
            // Also wake by the moment the display window expires, so we can
            // blank the panel promptly instead of waiting a full sample
            // interval before transitioning state.
            if (display_wake_until != INT64_MAX) {
                const int64_t until_off = display_wake_until - now2;
                if (until_off > 0 && until_off < sleep_ms) sleep_ms = until_off;
            }
        }
#endif
#ifdef CONFIG_CARL_DISPLAY_PROFILE
        // Wait for either the timeout OR a button press, whichever comes
        // first. Plain k_sleep() couldn't be interrupted by the GPIO ISR,
        // so a press during a 30-min idle (customer-mode normal cadence)
        // sat unread until the next scheduled sample woke the loop.
        carl::ui::button::waitForAnyEvent(static_cast<int>(sleep_ms));
#else
        k_sleep(K_MSEC(sleep_ms));
#endif
    }
    return 0;
}
