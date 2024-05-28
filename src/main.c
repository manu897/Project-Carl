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

/* including private headers */
#include "bme688_reg.h"

/* The code snippet */
/* 1000x30 = 30sec */
//#define SLEEP_TIME_MS	1000*2
#define I2C_NODE DT_NODELABEL (bme688)

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/sensor.h>

int main(void)
{
	// Code using I2C and registers

	printk(" Initiating the Plant Monitor (Project-Carl) Using Thingy:53 with board configuration: %s\n", CONFIG_BOARD);

	int ret;

	// Retrive the API-Specific device structure and make sure that the device is ready to use

	static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
	if (!device_is_ready(dev_i2c.bus))
	{
		printk("I2C bus %s is not ready!\n\r", dev_i2c.bus->name);
		return;
	}

	// Setup the Sensor

	// Read the sensor
	while (1)
	{
		printk("Main is running\n\r");
		k_msleep(SLEEP_TIME_MS);
	}

	/*// Test example (start) //

	printk("BME68x Example Thingy:53! board configuration: %s\n", CONFIG_BOARD);

	const struct device *bme = DEVICE_DT_GET_ONE(bosch_bme680);
	struct sensor_value temp, press, humidity, gas_res;


	if (!device_is_ready(bme)) 
	{
		printk("sensor: device not ready.\n");
		return;
	}
	printk("Device %p name is %s\n", bme, bme->name);

	while (1) {
		k_sleep(K_MSEC(1000));

		sensor_sample_fetch(bme);
		sensor_channel_get(bme, SENSOR_CHAN_AMBIENT_TEMP, &temp);
		sensor_channel_get(bme, SENSOR_CHAN_PRESS, &press);
		sensor_channel_get(bme, SENSOR_CHAN_HUMIDITY, &humidity);
		sensor_channel_get(bme, SENSOR_CHAN_GAS_RES, &gas_res);

		printk("T: %d.%06d | P: %d.%06d | H: %d.%06d | G: %d.%06d\n",
				temp.val1, temp.val2, press.val1, press.val2,
				humidity.val1, humidity.val2, gas_res.val1,
				gas_res.val2);
	} 
	// Test example (End) // */
}