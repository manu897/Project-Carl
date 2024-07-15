
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/gpio.h>
#include "ui.h"


LOG_MODULE_REGISTER(ui, LOG_LEVEL_INF);


/* LED1 pin */
static const struct gpio_dt_spec led0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec led1 = GPIO_DT_SPEC_GET(DT_ALIAS(led1), gpios);
static const struct gpio_dt_spec led2 = GPIO_DT_SPEC_GET(DT_ALIAS(led2), gpios);


void ui_init( void )
{
    /* init led0 pin */
    gpio_pin_configure_dt(&led0, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led1, GPIO_OUTPUT_ACTIVE);
	gpio_pin_configure_dt(&led2, GPIO_OUTPUT_ACTIVE);
    k_msleep(5000);
    ui_clear_leds();
}

bool ui_get_led0( void )
{
    return (gpio_pin_get_dt(&led0) ? true : false);
}

void ui_set_led0_brightness( uint8_t brightness )
{
    /*LOG_INF("Setting LED to brightness %d", brightness);*/
    if (brightness > 0) 
    {
        ui_set_led0();
    }
    else 
    {
        ui_clear_led0();
    }
}

void ui_set_led0( void )
{
    /*LOG_INF("LED ON");*/
    gpio_pin_set_dt(&led0, 1);
}

void ui_clear_leds( void )
{
    /*LOG_INF("LEDS OFF");*/
    gpio_pin_set_dt(&led0, 0);
    gpio_pin_set_dt(&led1, 0);
    gpio_pin_set_dt(&led2, 0);
}

void ui_toggle_led0( void )
{
    /*LOG_INF("Toggle LED");*/
    gpio_pin_toggle_dt(&led0); 
}

void ui_toggle_led1( void )
{
    /*LOG_INF("Toggle LED");*/
    gpio_pin_toggle_dt(&led1); 
}
void ui_toggle_led2( void )
{
    /*LOG_INF("Toggle LED");*/
    gpio_pin_toggle_dt(&led2); 
}

