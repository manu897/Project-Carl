/*******************************************************************
 * File: main.cpp
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-05-02
 * Description: C file for main.c in Project_Carl
*********************************************************************/
// #include <zephyr/kernel.h>
// #include <zephyr/device.h>
// #include <zephyr/devicetree.h>
// #include <zephyr/drivers/i2c.h>
// #include <zephyr/logging/log.h>
// #include <zephyr/sys/printk.h>
#include "bme688_reg.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>


#define I2C_NODE DT_NODELABEL (bme688)

static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);


void Configi2c(void)
{
    // Code using I2C and registers
    
	printk(" Initiating the Plant Monitor (Project-Carl) Using Thingy:53 with board configuration: %s\n", CONFIG_BOARD);
	// int ret;
	// Retrive the API-Specific device structure and make sure that the device is ready to use
	// static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
	if (!device_is_ready(dev_i2c.bus))
	{
		printk("I2C bus %s is not ready!\n\r", dev_i2c.bus->name);
		return;
	}
}

void EnvSensorConfig(void)
{
   // ******************** /// Forced Mode /// ******************** //
    int ret;
    //static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);
    // set humidity oversampling set osrs_x<2:0>
    char buff1[] = {BME688_CTRL_HUM, BME688_MODE_CTRL_HUM_DEFAULT};
    ret = i2c_write_dt(&dev_i2c, buff1, sizeof(buff1));
    //LOG_

    if (ret !=0)
    {
        printk("Failed to write to I2C device address 0x%c at Reg. 0x%c\n", dev_i2c.addr, BME688_CTRL_HUM);
    }
    // set temperature and pressure oversampling 
    char buff2[] = {BME688_CTRL_MEAS, BME688_MODE_CTRL_TEMP_PRESS_DEFAULT};
    ret = i2c_write_dt(&dev_i2c, buff2, sizeof(buff2));

    if (ret !=0)
    {
        printk("Failed to write to I2C device address 0x%c at Reg. 0x%c\n", dev_i2c.addr, BME688_CTRL_MEAS);
    }
    // set heat oversampling

    // select IIR filter for temperature sensor

    // select index of heater step

    // define heater-on time

    // set heater temperature

    // set mode to force mode
}


void EnvSensorRead(void)
{
    // declare the variables for incoming data

    // do a burst read of temperature
    // do a burst read of pressure
    // do a burst read of humidity (8s response time)    

    // print on terminal the values on floating point
}