/*******************************************************************
 * File: main.cpp
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-04-25
 * Description: Project Carl: Plant vital monitor, using arduino nRF SDK and Zypher RTOS.
*********************************************************************/
// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/printk.h>
// #include <zephyr/drivers/gpio.h>
// #include <zephyr/drivers/sensor.h>

// /* including private headers */
// #include "bme688_reg.h"
// #include "bme688_interface.h"
#include "ui.h"
#include "ui.c"


// /* The code snippet */
// /* 1000x30 = 30sec */
// #define SLEEP_TIME_MS	1000/6
// // #define I2C_NODE DT_NODELABEL (bme688)

// // #include <zephyr/drivers/sensor.h>

// int main(void)
// {

// 	// Configure I2C
// 	if(false == configi2c())
// 	{
// 		printk("FAIL to init configure I2C settings\n\r");
// 		return;
// 	}

// 	// Setup the Sensor
// 	if (!envSensorConfig())
// 	{
// 		printk("FAIL to init configure Sensor settings\n\r");
// 		return;
// 	}

// 	// Setup the UI
// 	if (rgb_led_init(&rgb_led, &rgb_config) != 0) 
// 	{
//         LOG_ERR("Failed to initialize RGB LED");
//         return false;
//     }
// 	ui_error();
// 	lost_ble();
// 	// Read the sensor
// 	while (1)
// 	{
// 		if (!envSensorRead())
// 		{
// 			printk("FAIL to read sensor data\n\r");
// 			return;
// 		}
// 		k_sleep(K_SECONDS(10));  // Sleep for 10 seconds

// 		// Example of RGB LED
// 		// rgb_led_set_color(&rgb_led, 1, 0, 0); // Red
// 		// k_msleep(SLEEP_TIME_MS);
//         // rgb_led_set_color(&rgb_led, 0, 1, 0); // Green
//         // k_msleep(SLEEP_TIME_MS);
//         // rgb_led_set_color(&rgb_led, 0, 0, 1); // Blue
//         // k_msleep(SLEEP_TIME_MS);
//         // rgb_led_off(&rgb_led); // Off
//         // k_msleep(SLEEP_TIME_MS);
// 	}

// 	return 0;
// }

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/gpio.h>

void main(void)
{

	const struct device *bme = DEVICE_DT_GET_ONE(bosch_bme680);
	// struct sensor_value temp, press, humidity, gas_res;
	struct sensor_value temp, press, humidity;

	printk("Initiating Project-Carl, Plant Monitor using Thingy:53 %s\n", CONFIG_BOARD);

	if (!device_is_ready(bme)) {
		printk("BME680 sensor: Device not ready.\n");
	} else {
		printk("BME680 sensor: Device is ready.\n");
	}


	// Setup the UI
	if (rgb_led_init(&rgb_led, &rgb_config) != 0) 
	{
        LOG_ERR("Failed to initialize RGB LED");
		ui_error();
        return false;
    }

	led_check();
	welcome_light();

	if (!device_is_ready(bme)) {
		printk("sensor: Device not ready.\n");
		return;
	}
	printk("Device %p name is %s\n", bme, bme->name);

	// if (!device_is_ready(battery)) {
	// 	printk("Battery sensor: Device not ready.\n");
	// 	return;
	// }

	while (1) {
		k_sleep(K_MINUTES(0.5));

		sensor_sample_fetch(bme);
		sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(bme, SENSOR_CHAN_PRESS, &press);
		sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity);
		// sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas_res);

		// Multiply pressure value by 10 to shift the decimal point
		press.val1 = press.val1 * 10 + press.val2 / 100000;
		press.val2 = (press.val2 % 100000) * 10;

		/* Commenting out the Gas sensor */
		// printk("Temp: %d.%06d °C | Atm: %d.%06d hPa | Hum: %d.%06d%% | Gas: %d.%06d Ω\n",
		// 		temp.val1, temp.val2, press.val1, press.val2,
		// 		humidity.val1, humidity.val2, gas_res.val1,
		// 		gas_res.val2);

		printk("Temp: %d.%06d °C | Atm: %d.%06d hPa | Hum: %d.%06d%%\n",
					temp.val1, temp.val2, press.val1, press.val2,
					humidity.val1, humidity.val2);
		// Flash green light
		rgb_led_set_color(&rgb_led, 0, 1, 0); // Green
		k_msleep(500);
		rgb_led_off(&rgb_led); // Off

	}
}
