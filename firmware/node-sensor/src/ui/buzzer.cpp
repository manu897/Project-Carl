#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "ui/buzzer.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>

namespace carl::ui::buzzer {

namespace {

const struct pwm_dt_spec g_pwm = PWM_DT_SPEC_GET(DT_ALIAS(carl_buzzer));

void beep(int ms) {
    pwm_set_dt(&g_pwm, PWM_HZ(2000), PWM_HZ(2000) / 2);  // 50% duty
    k_msleep(ms);
    pwm_set_dt(&g_pwm, 0, 0);
}

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

}  // namespace carl::ui::buzzer

#endif  // CONFIG_CARL_DISPLAY_PROFILE
