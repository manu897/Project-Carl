#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "ui/buzzer.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

namespace carl::ui::buzzer {

namespace {

const struct pwm_dt_spec g_pwm = PWM_DT_SPEC_GET(DT_ALIAS(carl_buzzer));

void tone(uint32_t hz, int ms) {
    if (hz == 0) {
        pwm_set_dt(&g_pwm, 0, 0);
        k_msleep(ms);
        return;
    }
    pwm_set_dt(&g_pwm, PWM_HZ(hz), PWM_HZ(hz) / 2);  // 50% duty
    k_msleep(ms);
    pwm_set_dt(&g_pwm, 0, 0);
}

void beep(int ms) { tone(2000, ms); }

}  // namespace

bool init() { return device_is_ready(g_pwm.dev); }

void alert(carl::thresholds::Severity s) {
    using S = carl::thresholds::Severity;
    if (s == S::kOk) return;
    if (s == S::kWarning) {
        beep(120);
        return;
    }
    // Critical: triple beep.
    for (int i = 0; i < 3; ++i) {
        beep(80);
        k_msleep(80);
    }
}

void welcomeChime() {
    // The Nokia tune — the second-to-last phrase of Francisco Tárrega's
    // "Gran Vals" (1902, public domain). Thirteen notes, ~3 seconds total.
    // Eighth + eighth + quarter + quarter rhythm, repeated three times,
    // ending on a half note.
    struct Note { uint16_t hz; uint16_t ms; };
    static constexpr Note kNokiaTune[] = {
        {659, 130}, {587, 130}, {370, 260}, {415, 260},  // E5  D5  F#4 G#4
        {554, 130}, {494, 130}, {294, 260}, {330, 260},  // C#5 B4  D4  E4
        {494, 130}, {440, 130}, {554, 260}, {659, 260},  // B4  A4  C#5 E5
        {440, 520},                                       // A4  (half)
    };
    for (const auto& n : kNokiaTune) {
        tone(n.hz, n.ms);
        k_msleep(20);  // small gap for note articulation
    }
}

}  // namespace carl::ui::buzzer

#endif  // CONFIG_CARL_DISPLAY_PROFILE
