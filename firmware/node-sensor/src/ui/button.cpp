#ifdef CONFIG_CARL_DISPLAY_PROFILE

#include "ui/button.h"

#include <atomic>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>

namespace carl::ui::button {

namespace {

constexpr int kLongPressMs = 2000;

const struct gpio_dt_spec g_btn = GPIO_DT_SPEC_GET(DT_ALIAS(carl_button), gpios);

std::atomic<bool> g_short_latched{false};
std::atomic<bool> g_long_latched{false};
int64_t g_press_started_ms = 0;

struct gpio_callback g_cb;

void edgeCb(const struct device*, struct gpio_callback*, gpio_port_pins_t) {
    const int pressed = gpio_pin_get_dt(&g_btn);
    const int64_t now = k_uptime_get();
    if (pressed) {
        g_press_started_ms = now;
    } else {
        const int64_t held = now - g_press_started_ms;
        if (held >= kLongPressMs)        g_long_latched.store(true);
        else if (held >= 30)             g_short_latched.store(true);  // debounce
    }
}

}  // namespace

bool init() {
    if (!device_is_ready(g_btn.port)) return false;
    if (gpio_pin_configure_dt(&g_btn, GPIO_INPUT) != 0) return false;
    if (gpio_pin_interrupt_configure_dt(&g_btn, GPIO_INT_EDGE_BOTH) != 0) return false;
    gpio_init_callback(&g_cb, edgeCb, BIT(g_btn.pin));
    gpio_add_callback(g_btn.port, &g_cb);
    return true;
}

bool consumeShortPress() { return g_short_latched.exchange(false); }
bool consumeLongPress()  { return g_long_latched.exchange(false); }

bool isHeldNow() { return gpio_pin_get_dt(&g_btn) > 0; }

}  // namespace carl::ui::button

#endif  // CONFIG_CARL_DISPLAY_PROFILE
