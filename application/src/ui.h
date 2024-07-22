
#ifndef __UI_H
#define __UI_H


#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>


// extern void ui_init(void);
// extern bool ui_get_led0(void);
// extern void ui_set_led0_brightness(uint8_t brightness);
// extern void ui_set_led0(void);
// extern void ui_clear_leds(void);
// extern void ui_toggle_led0(void);

typedef struct 
{
    struct gpio_dt_spec red_led;
    struct gpio_dt_spec green_led;
    struct gpio_dt_spec blue_led;
} rgb_led_config_t;

typedef struct 
{
    rgb_led_config_t config;
} rgb_led_t;

int rgb_led_init(rgb_led_t *led, const rgb_led_config_t *config);
void rgb_led_set_color(rgb_led_t *led, uint8_t red, uint8_t green, uint8_t blue);
void rgb_led_off(rgb_led_t *led);
void ui_error(void);
void lost_ble(void);
extern rgb_led_t rgb_led;


#endif