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

// /* 1000 msec = 1 sec */
// #define SLEEP_TIME_MS   1000

// /*******************************************************************
//  * Function Declarations
//  *******************************************************************/
// void WelcomeSplash(void);
// void EnvSensorRead(void);

/*******************************************************************
 * Define the address of relevant sensor registers and settings
 *******************************************************************/
#define I2C_NODE DT_NODELABEL (BME680)

#define BME688_STATUS            0x73
#define BME688_RESET             0xE0
#define BME688_CHIP_ID           0xD0
#define BME688_CONFIG            0x75
#define BME688_CTRL_MEAS         0x74   // Sensor power mode 1. 00 = sleep mode, 2. 01 = Forced mode, 3. 10 = Parallel mode.
#define BME688_CTRL_HUM          0x72   

/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

// #include <zephyr/zephyr.h>
#include <zephyr/drivers/sensor.h>

void main(void)
{
	printk("BME68x Example Thingy:53! %s\n", CONFIG_BOARD);

	const struct device *bme = DEVICE_DT_GET_ONE(bosch_bme680);
	struct sensor_value temp, press, humidity, gas_res;


	if (!device_is_ready(bme)) {
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
}