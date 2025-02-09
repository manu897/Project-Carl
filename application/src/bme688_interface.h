/*******************************************************************
 * File: bme688_interface.h
 * Author: Manideep Reddy Tamma
 * Date of creation: 2025-02-08
 * Description: header file for bme688_interface
*********************************************************************/

#ifndef BME688_INTERFACE_H
#define BME688_INTERFACE_H

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>

bool configi2c(void);
bool envSensorConfig(void);
bool envSensorRead(void);

#endif // BME688_INTERFACE_H