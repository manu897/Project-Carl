/*******************************************************************
 * File: bme688_interface.c
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-05-02
 * Description: C file for interfacing with BME688 sensor in Project Carl
*********************************************************************/

#include "bme688_reg.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

#define I2C_NODE DT_NODELABEL(bme688)

static const struct i2c_dt_spec dev_i2c = I2C_DT_SPEC_GET(I2C_NODE);

// Calibration parameters
uint16_t par_t1, par_t2;
int8_t par_t3;
uint16_t par_p1, par_p2;
int8_t par_p3, par_p4, par_p5, par_p6, par_p7, par_p8, par_p9, par_p10;
uint16_t par_h1, par_h2;
int8_t par_h3, par_h4, par_h5, par_h6, par_h7;

int32_t t_fine;

bool read_calibration_params()
{
    uint8_t calib_data[41];
    int ret = i2c_burst_read_dt(&dev_i2c, 0xE1, calib_data, sizeof(calib_data));
    if (ret != 0)
    {
        printk("Failed to read calibration data from I2C device address 0x%x\n", dev_i2c.addr);
        return false;
    }

    par_t1 = (uint16_t)((calib_data[33] << 8) | calib_data[32]);
    par_t2 = (uint16_t)((calib_data[1] << 8) | calib_data[0]);
    par_t3 = (int8_t)calib_data[2];

    par_p1 = (uint16_t)((calib_data[5] << 8) | calib_data[4]);
    par_p2 = (uint16_t)((calib_data[7] << 8) | calib_data[6]);
    par_p3 = (int8_t)calib_data[8];
    par_p4 = (int8_t)calib_data[9];
    par_p5 = (int8_t)calib_data[10];
    par_p6 = (int8_t)calib_data[11];
    par_p7 = (int8_t)calib_data[12];
    par_p8 = (int8_t)calib_data[13];
    par_p9 = (int8_t)calib_data[14];
    par_p10 = (int8_t)calib_data[15];

    par_h1 = (uint16_t)((calib_data[26] << 4) | (calib_data[25] & 0x0F));
    par_h2 = (uint16_t)((calib_data[27] << 4) | (calib_data[25] >> 4));
    par_h3 = (int8_t)calib_data[28];
    par_h4 = (int8_t)calib_data[29];
    par_h5 = (int8_t)calib_data[30];
    par_h6 = (int8_t)calib_data[31];
    par_h7 = (int8_t)calib_data[32];

    printk("Calibration parameters:\n");
    printk("par_t1: %u, par_t2: %u, par_t3: %d\n", par_t1, par_t2, par_t3);
    printk("par_p1: %u, par_p2: %u, par_p3: %d, par_p4: %d, par_p5: %d, par_p6: %d, par_p7: %d, par_p8: %d, par_p9: %d, par_p10: %d\n",
            par_p1, par_p2, par_p3, par_p4, par_p5, par_p6, par_p7, par_p8, par_p9, par_p10);
    printk("par_h1: %u, par_h2: %u, par_h3: %d, par_h4: %d, par_h5: %d, par_h6: %d, par_h7: %d\n",
            par_h1, par_h2, par_h3, par_h4, par_h5, par_h6, par_h7);

    return true;
}

bool configi2c()
{
    printk("Initiating the Plant Monitor (Project-Carl) Using Thingy:53 with board configuration: %s\n", CONFIG_BOARD);
    if (!device_is_ready(dev_i2c.bus))
    {
        printk("I2C bus %s is not ready!\n", dev_i2c.bus->name);
        return false;
    }
    printk("I2C bus %s is ready\n", dev_i2c.bus->name);
    return true;
}

bool envSensorConfig(void)
{
    int ret;
    char buff1[] = {BME688_CTRL_HUM, BME688_MODE_CTRL_HUM_DEFAULT};
    ret = i2c_write_dt(&dev_i2c, buff1, sizeof(buff1));
    if (ret != 0)
    {
        printk("Failed to write to I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_CTRL_HUM);
        return false;
    }

    char buff2[] = {BME688_CTRL_MEAS, BME688_MODE_CTRL_TEMP_PRESS_DEFAULT};
    ret = i2c_write_dt(&dev_i2c, buff2, sizeof(buff2));
    if (ret != 0)
    {
        printk("Failed to write to I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_CTRL_MEAS);
        return false;
    }

    char buff3[] = {BME688_CTRL_MEAS, 0x01}; // Forced mode
    ret = i2c_write_dt(&dev_i2c, buff3, sizeof(buff3));
    if (ret != 0)
    {
        printk("Failed to set sensor to forced mode at I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_CTRL_MEAS);
        return false;
    }

    if (!read_calibration_params())
    {
        printk("Failed to read calibration parameters\n");
        return false;
    }

    return true;
}

double compensate_temperature(int32_t temp_raw)
{
    double var1 = (((double)temp_raw / 16384.0) - ((double)par_t1 / 1024.0)) * (double)par_t2;
    double var2 = ((((double)temp_raw / 131072.0) - ((double)par_t1 / 8192.0)) * (((double)temp_raw / 131072.0) - ((double)par_t1 / 8192.0))) * ((double)par_t3 * 16.0);
    t_fine = (int32_t)(var1 + var2);
    return t_fine / 5120.0;
}

double compensate_pressure(int32_t press_raw)
{
    double var1 = ((double)t_fine / 2.0) - 64000.0;
    double var2 = var1 * var1 * ((double)par_p6 / 131072.0);
    var2 = var2 + (var1 * (double)par_p5 * 2.0);
    var2 = (var2 / 4.0) + ((double)par_p4 * 65536.0);
    var1 = (((double)par_p3 * var1 * var1 / 16384.0) + ((double)par_p2 * var1)) / 524288.0;
    var1 = (1.0 + (var1 / 32768.0)) * (double)par_p1;
    double press_comp = 1048576.0 - (double)press_raw;
    press_comp = (press_comp - (var2 / 4096.0)) * 6250.0 / var1;
    var1 = ((double)par_p9 * press_comp * press_comp) / 2147483648.0;
    var2 = press_comp * ((double)par_p8 / 32768.0);
    double var3 = (press_comp / 256.0) * (press_comp / 256.0) * (press_comp / 256.0) * (par_p10 / 131072.0);
    press_comp = press_comp + (var1 + var2 + var3 + ((double)par_p7 * 128.0)) / 16.0;
    return press_comp;
}

double compensate_humidity(int32_t hum_raw, double temp_comp)
{
    double var1 = hum_raw - (((double)par_h1 * 16.0) + (((double)par_h3 / 2.0) * temp_comp));
    double var2 = var1 * (((double)par_h2 / 262144.0) * (1.0 + (((double)par_h4 / 16384.0) * temp_comp) + (((double)par_h5 / 1048576.0) * temp_comp * temp_comp)));
    double var3 = (double)par_h6 / 16384.0;
    double var4 = (double)par_h7 / 2097152.0;
    double hum_comp = var2 + ((var3 + (var4 * temp_comp)) * var2 * var2);
    return hum_comp;
}

bool envSensorRead(void)
{
    uint8_t temp_data[3];
    uint8_t press_data[3];
    uint8_t hum_data[2];

    int ret = i2c_burst_read_dt(&dev_i2c, BME688_TEMP_MSB_0, temp_data, sizeof(temp_data));
    if (ret != 0)
    {
        printk("Failed to read from I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_TEMP_MSB_0);
        return false;
    }

    ret = i2c_burst_read_dt(&dev_i2c, BME688_PRESS_MSB_0, press_data, sizeof(press_data));
    if (ret != 0)
    {
        printk("Failed to read from I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_PRESS_MSB_0);
        return false;
    }

    ret = i2c_burst_read_dt(&dev_i2c, BME688_HUM_MSB_0, hum_data, sizeof(hum_data));
    if (ret != 0)
    {
        printk("Failed to read from I2C device address 0x%x at Reg. 0x%x\n", dev_i2c.addr, BME688_HUM_MSB_0);
        return false;
    }

    int32_t temp_raw = (int32_t)((temp_data[0] << 12) | (temp_data[1] << 4) | (temp_data[2] >> 4));
    int32_t press_raw = (int32_t)((press_data[0] << 12) | (press_data[1] << 4) | (press_data[2] >> 4));
    int32_t hum_raw = (int32_t)((hum_data[0] << 8) | hum_data[1]);

    double temp = compensate_temperature(temp_raw);
    double press = compensate_pressure(press_raw);
    double hum = compensate_humidity(hum_raw, temp);

    printk("Temperature: %.2f C\n", temp);
    printk("Pressure: %.2f hPa\n", press);
    printk("Humidity: %.2f %%\n", hum);

    return true;
}
