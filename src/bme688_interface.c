/*******************************************************************
 * File: main.cpp
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-05-02
 * Description: C file for main.c in Project_Carl
*********************************************************************/
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>

#include "bme688_reg.h"

void EnvSensorConfig(void)
{
   // ******************** /// Forced Mode /// ******************** //

    // set humidity oversampling
    // set temperature oversampling
    // set heat oversampling

    // select IIR filter for temperature sensor

    // select index of heater step

    // define heater-on time

    // set heater temperature

    // set mode to force mode
}


void EnvSensorRead(void)
{

}