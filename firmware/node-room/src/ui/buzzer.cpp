#include "ui/buzzer.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace carl::room::ui::buzzer {

namespace {

const struct pwm_dt_spec g_pwm = PWM_DT_SPEC_GET(DT_ALIAS(carl_buzzer));

}  // namespace

bool init() {
    if (!device_is_ready(g_pwm.dev)) {
        printk("room-buzzer: PWM not ready\n");
        return false;
    }
    printk("room-buzzer: ready\n");
    return true;
}

void tone(uint32_t hz, int ms) {
    if (hz == 0) {
        pwm_set_dt(&g_pwm, 0, 0);
        k_msleep(ms);
        return;
    }
    pwm_set_dt(&g_pwm, PWM_HZ(hz), PWM_HZ(hz) / 2);  // 50 % duty
    k_msleep(ms);
    pwm_set_dt(&g_pwm, 0, 0);
}

void off() {
    pwm_set_dt(&g_pwm, 0, 0);
}

void welcomeChime() {
    // Nokia tune — the second-to-last phrase of Francisco Tarrega's
    // "Gran Vals" (1902, public domain). Same as firmware/node-sensor/.
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

void errorBeep() {
    // Three fast descending tones — clearly distinct from the welcome chime.
    tone(1200, 100);
    k_msleep(40);
    tone(800, 100);
    k_msleep(40);
    tone(500, 150);
}

}  // namespace carl::room::ui::buzzer
