
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
//#include <zephyr/drivers/gpio.h>
#include "ui.h"


LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);


/* LED1 pin */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);


int rgb_led_init(rgb_led_t *led, const rgb_led_config_t *config) 
{
    if (!device_is_ready(config->red_led.port) ||
        !device_is_ready(config->green_led.port) ||
        !device_is_ready(config->blue_led.port)) {
        return -ENODEV;
    }

    led->config = *config;

    gpio_pin_configure_dt(&led->config.red_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led->config.green_led, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led->config.blue_led, GPIO_OUTPUT_INACTIVE);

    return 0;
}

void rgb_led_set_color(rgb_led_t *led, uint8_t red, uint8_t green, uint8_t blue) 
{
    gpio_pin_set_dt(&led->config.red_led, red);
    gpio_pin_set_dt(&led->config.green_led, green);
    gpio_pin_set_dt(&led->config.blue_led, blue);
}

void rgb_led_off(rgb_led_t *led) 
{
    gpio_pin_set_dt(&led->config.red_led, 0);
    gpio_pin_set_dt(&led->config.green_led, 0);
    gpio_pin_set_dt(&led->config.blue_led, 0);
}