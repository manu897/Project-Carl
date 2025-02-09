/*******************************************************************
 * File: main.cpp
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-04-25
 * Description: Project Carl: Plant vital monitor, using arduino nRF SDK and Zypher RTOS.
*********************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/drivers/gpio.h>

/* including private headers */
#include "bme688_reg.h"
#include "bme688_interface.h"
#include "ui.h"
#include "ui.c"


/* The code snippet */
/* 1000x30 = 30sec */
#define SLEEP_TIME_MS	1000/6
// #define I2C_NODE DT_NODELABEL (bme688)

// #include <zephyr/drivers/sensor.h>

int main(void)
{

	// Configure I2C
	if(false == configi2c())
	{
		printk("FAIL to init configure I2C settings\n\r");
		return;
	}

	// Setup the Sensor
	if (!envSensorConfig())
	{
		printk("FAIL to init configure Sensor settings\n\r");
		return;
	}

	// Setup the UI
	if (rgb_led_init(&rgb_led, &rgb_config) != 0) 
	{
        LOG_ERR("Failed to initialize RGB LED");
        return false;
    }
	ui_error();
	lost_ble();
	// Read the sensor
	while (1)
	{
		if (!envSensorRead())
		{
			printk("FAIL to read sensor data\n\r");
			return;
		}
		k_sleep(K_SECONDS(10));  // Sleep for 10 seconds

		// Example of RGB LED
		// rgb_led_set_color(&rgb_led, 1, 0, 0); // Red
		// k_msleep(SLEEP_TIME_MS);
        // rgb_led_set_color(&rgb_led, 0, 1, 0); // Green
        // k_msleep(SLEEP_TIME_MS);
        // rgb_led_set_color(&rgb_led, 0, 0, 1); // Blue
        // k_msleep(SLEEP_TIME_MS);
        // rgb_led_off(&rgb_led); // Off
        // k_msleep(SLEEP_TIME_MS);
	}

	return 0;
}

