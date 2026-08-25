#include "ui/led.h"

#include <zephyr/devicetree.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace carl::room::ui::led {

namespace {

// The Thingy:53 board DTS defines pwm-led0..2 as aliases for the RGB LED.
const struct pwm_dt_spec g_red   = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led0));
const struct pwm_dt_spec g_green = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led1));
const struct pwm_dt_spec g_blue  = PWM_DT_SPEC_GET(DT_ALIAS(pwm_led2));

void setPwmBrightness(const struct pwm_dt_spec* spec, uint8_t brightness) {
    uint32_t period = spec->period;
    uint32_t duty   = period * brightness / 255;
    pwm_set_dt(spec, period, duty);
}

}  // namespace

bool init() {
    bool ok = true;
    if (!device_is_ready(g_red.dev))   { printk("room-led: red PWM not ready\n");   ok = false; }
    if (!device_is_ready(g_green.dev)) { printk("room-led: green PWM not ready\n"); ok = false; }
    if (!device_is_ready(g_blue.dev))  { printk("room-led: blue PWM not ready\n");  ok = false; }
    if (ok) printk("room-led: RGB LED ready\n");
    return ok;
}

void setRGB(uint8_t r, uint8_t g, uint8_t b) {
    setPwmBrightness(&g_red,   r);
    setPwmBrightness(&g_green, g);
    setPwmBrightness(&g_blue,  b);
}

void off() {
    setRGB(0, 0, 0);
}

void flashGreen(int ms) {
    setRGB(0, 200, 0);
    k_msleep(ms);
    off();
}

void flashRed(int ms) {
    setRGB(255, 0, 0);
    k_msleep(ms);
    off();
}

void blinkStatus(uint8_t r, uint8_t g, uint8_t b, int count, int on_ms, int gap_ms) {
    for (int i = 0; i < count; ++i) {
        setRGB(r, g, b);
        k_msleep(on_ms);
        off();
        if (i + 1 < count) k_msleep(gap_ms);
    }
}

}  // namespace carl::room::ui::led
