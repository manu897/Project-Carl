#include "ui/rgb.h"

#ifdef CONFIG_CARL_HAS_RGB

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>

namespace carl::ui::rgb {

namespace {

// XIAO nRF52840 onboard RGB LED: led0=red (P0.26), led1=green (P0.30),
// led2=blue (P0.06), all GPIO_ACTIVE_LOW. The board DTS provides the aliases.
const struct gpio_dt_spec g_red   = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
const struct gpio_dt_spec g_green = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
const struct gpio_dt_spec g_blue  = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);

bool g_ready = false;

// gpio_dt_spec honours the ACTIVE_LOW flag: logical 1 = "on" = pin driven low.
void set(bool r, bool g, bool b) {
    if (!g_ready) return;
    gpio_pin_set_dt(&g_red,   r ? 1 : 0);
    gpio_pin_set_dt(&g_green, g ? 1 : 0);
    gpio_pin_set_dt(&g_blue,  b ? 1 : 0);
}

}  // namespace

bool init() {
    g_ready = gpio_is_ready_dt(&g_red)
           && gpio_is_ready_dt(&g_green)
           && gpio_is_ready_dt(&g_blue);
    if (!g_ready) {
        printk("rgb: LED GPIOs not ready\n");
        return false;
    }
    // Start off (logical inactive).
    gpio_pin_configure_dt(&g_red,   GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&g_green, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&g_blue,  GPIO_OUTPUT_INACTIVE);
    printk("rgb: onboard RGB LED ready\n");
    return true;
}

void off() { set(false, false, false); }

namespace {
// One on/off pulse of the given colour. Kept short so battery duty stays tiny.
void pulse(bool r, bool g, bool b, int on_ms, int gap_ms) {
    set(r, g, b);
    k_msleep(on_ms);
    set(false, false, false);
    k_msleep(gap_ms);
}
}  // namespace

void blinkSeverity(carl::thresholds::Severity s) {
    if (!g_ready) return;
    using S = carl::thresholds::Severity;
    switch (s) {
        case S::kCritical:  // three red blinks
            for (int i = 0; i < 3; ++i) pulse(true, false, false, 120, 120);
            break;
        case S::kWarning:   // two amber blinks
            for (int i = 0; i < 2; ++i) pulse(true, true, false, 120, 120);
            break;
        case S::kOk:
        default:            // one short green "alive + healthy" blink
            pulse(false, true, false, 80, 0);
            break;
    }
    off();  // never leave the LED lit between samples
}

void bootFlash() {
    if (!g_ready) return;
    // Quick R → G → B → white → off sweep so the user sees the node boot.
    set(true,  false, false); k_msleep(120);
    set(false, true,  false); k_msleep(120);
    set(false, false, true);  k_msleep(120);
    set(true,  true,  true);  k_msleep(180);
    off();
}

}  // namespace carl::ui::rgb

#else  // !CONFIG_CARL_HAS_RGB

namespace carl::ui::rgb {
bool init() { return false; }
void off() {}
void blinkSeverity(carl::thresholds::Severity) {}
void bootFlash() {}
}  // namespace carl::ui::rgb

#endif
