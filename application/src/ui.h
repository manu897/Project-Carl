
#ifndef __UI_H
#define __UI_H


#include <zephyr/kernel.h>


extern void ui_init(void);
extern bool ui_get_led0(void);
extern void ui_set_led0_brightness(uint8_t brightness);
extern void ui_set_led0(void);
extern void ui_clear_leds(void);
extern void ui_toggle_led0(void);


#endif