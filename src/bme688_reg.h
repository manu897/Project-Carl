/*******************************************************************
 * File: bme688_reg.h
 * Author: Manideep Reddy Tamma
 * Date of creation: 2024-04-30
 * Description:  header file for bme688_interface
 * Reference for the BME688 sensor manufacturer document https://www.bosch-sensortec.com/media/boschsensortec/downloads/datasheets/bst-bme688-ds000.pdf
 *******************************************************************/


/*******************************************************************
 * defines
 *******************************************************************/
/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

// Get the I2C Node identifier of the sensor
#define I2C_NODE DT_NODELABEL (BME680)

// Sensor registers addresses and settings

// Control
#define BME688_STATUS            0x73
#define BME688_RESET             0xE0
#define BME688_CHIP_ID           0xD0
#define BME688_CONFIG            0x75
#define BME688_CTRL_MEAS         0x74   // Sensor power mode 1. 00 = sleep mode, 2. 01 = Forced mode, 3. 10 = Parallel mode.
#define BME688_CTRL_HUM          0x72   // Controls over sampling setting of humidity sensor 1. 000 = Skipped (out set to 0x8000), 2. 001 = Oversampling x 1, 3. 010 = x2, 4. 011 = x4, 5. 100 = x8, 6. 101,others = x16.
#define BME688_CTRL_GAS_1        0X71
#define BME688_CTRL_GAS_0        0X70

// Read
#define BME688_HUM_LSB_0         0x26   // 16bit
#define BME688_HUM_MSB_0         0x25   // 8bit

#define BME688_TEMP_XLSB_0       0x24   // 20bit
#define BME688_TEMP_LSB_0        0x23   // 16bit
#define BME688_TEMP_MSB_0        0x22   // 8bit

#define BME688_PRESS_XLSB_0      0x21   // 20bit
#define BME688_PRESS_LSB_0       0x20   // 16bit
#define BME688_PRESS_MSB_0       0x1F   // 8bit



/*******************************************************************
 * Function Declarations
 *******************************************************************/
void WelcomeSplash(void);
void EnvSensorWrite(void);
void EnvSensorRead(void);