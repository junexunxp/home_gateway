#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sdkconfig.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "gw_sys_config.h"
#include "esp_log.h"
#include "linkkit_gateway.h"
#include "aht_10.h"

#if ENABLE_SENSOR_MODULE
// Specify the constants for water vapor and barometric pressure.
#define WATER_VAPOR 17.62f
#define BAROMETRIC_PRESSURE 243.5f


uint8_t eSensorCalibrateCmd[3] = {0xE1, 0x08, 0x00};
uint8_t eSensorNormalCmd[3]    = {0xA8, 0x00, 0x00};
uint8_t eSensorMeasureCmd[3]   = {0xAC, 0x33, 0x00};
uint8_t eSensorResetCmd        = 0xBA;

/**
 * @brief test code to read esp-i2c-slave
 *        We need to fill the buffer of esp slave device, then master can read them out.
 *
 * _______________________________________________________________________________________
 * | start | slave_addr + rd_bit +ack | read n-1 bytes + ack | read 1 byte + nack | stop |
 * --------|--------------------------|----------------------|--------------------|------|
 *
 */
static esp_err_t i2c_master_read_slave(i2c_port_t i2c_num, uint8_t *data_rd, size_t size)
{
    if (size == 0) {
        return ESP_OK;
    }
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (eAHT10Address_default << 1) | READ_BIT, ACK_CHECK_EN);
    if (size > 1) {
        i2c_master_read(cmd, data_rd, size - 1, ACK_VAL);
    }
    i2c_master_read_byte(cmd, data_rd + size - 1, NACK_VAL);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}

/**
 * @brief Test code to write esp-i2c-slave
 *        Master device write data to slave(both esp32),
 *        the data will be stored in slave buffer.
 *        We can read them out from slave buffer.
 *
 * ___________________________________________________________________
 * | start | slave_addr + wr_bit + ack | write n bytes + ack  | stop |
 * --------|---------------------------|----------------------|------|
 *
 */
static esp_err_t i2c_master_write_slave(i2c_port_t i2c_num, uint8_t *data_wr, size_t size)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (eAHT10Address_default << 1) | WRITE_BIT, ACK_CHECK_EN);
    i2c_master_write(cmd, data_wr, size, ACK_CHECK_EN);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);
    return ret;
}



static int aht_read_value(bool get_temp);

/**********************************************************
 * GetHumidity
 *  Gets the current humidity from the sensor.
 *
 * @return float - The relative humidity in %RH
 **********************************************************/
double aht_get_humidity(void )
{
    int value = aht_read_value(false);
    if (value == 0) {
        return 0;                       // Some unrealistic value
    }
	double ret = ((double )value * 100 / 1048576);
    return ret;
}

/**********************************************************
 * GetTemperature
 *  Gets the current temperature from the sensor.
 *
 * @return float - The temperature in Deg C
 **********************************************************/
float aht_get_tem(void)
{
    int value = aht_read_value(true);
	float ret = ((float)((float )(200 * value) / 1048576) - 50);
    return ret;
}

/**********************************************************
 * GetDewPoint
 *  Gets the current dew point based on the current humidity and temperature
 *
 * @return float - The dew point in Deg C
 **********************************************************/
float aht_get_dew_point(float humidity, float temperature)
{
  

  // Calculate the intermediate value 'gamma'
 float gamma = (humidity / 100) + WATER_VAPOR * temperature / (BAROMETRIC_PRESSURE + temperature);
  // Calculate dew point in Celsius
 float dewPoint = BAROMETRIC_PRESSURE * gamma / (WATER_VAPOR - gamma);

 return dewPoint;
}

/******************************************************************************
 * Private Functions
 ******************************************************************************/

static int aht_read_value(bool get_temp)
{
    int result;
	int write_st = i2c_master_write_slave(0,eSensorMeasureCmd,sizeof(eSensorMeasureCmd));
	if(write_st){
		result = write_st;
		goto error;
	}

    uint8_t read_data[6] = {0};
	int read_st = i2c_master_read_slave(0,&read_data,sizeof(read_data));
	if(read_st){
		result = read_st;
		goto error;
	}
 	if(get_temp){
		result = ((read_data[3] & 0x0F) << 16) | (read_data[4] << 8) | read_data[5];
 	}else{
    	result = ((read_data[1] << 16) | (read_data[2] << 8) | read_data[3]) >> 4;
 	}
    
	error:
    return result;
}

static uint8_t aht_read_status(void)
{
	uint8_t read_data = 0;
	int read_st = i2c_master_read_slave(0,&read_data,1);
	if(read_st){
		return 0xff;
	}
    return read_data;
}

/**
 * @brief i2c master initialization
 */
static esp_err_t i2c_master_init()
{
    int i2c_master_port = I2C_MASTER_NUM;
    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_MASTER_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_MASTER_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_MASTER_FREQ_HZ;
    i2c_param_config(i2c_master_port, &conf);
    return i2c_driver_install(i2c_master_port, conf.mode,
                              I2C_MASTER_RX_BUF_DISABLE,
                              I2C_MASTER_TX_BUF_DISABLE, 0);
}

static int aht_setup(void)
{
	//hal_i2c_master_write(0, &pdata, 100,1);
	i2c_master_write_slave(0, &eSensorCalibrateCmd, sizeof(eSensorCalibrateCmd));
	vTaskDelay(50 / portTICK_PERIOD_MS);
	if((aht_read_status() & 0x68) == 0x08){
		return 0;
	}else{
		return -1;
	}
}
static char humidity_l[8], temperature_l[8];
void read_temperature(void *pv_parameters)
{

    double humidity = aht_get_humidity();
	double temperature = aht_get_tem();
	char hum[8] = {0};
	char tem[8] = {0};
	
	sprintf(hum,"%.1f",humidity);
	sprintf(tem,"%.1f",temperature);

	if((memcmp(hum,humidity_l,8) == 0) && (memcmp(tem,temperature_l,8) == 0)){
		return;
	}
	memcpy(humidity_l,hum,8);
	memcpy(temperature_l,tem,8);
	//humidity_l = humidity;
	//temperature_l = temperature;

	if(linkkit_cloud_connected()){
		tem_sensor_property_post(temperature,humidity);
	}else{

		printf("cloud not connected\r\n");
		
	}
	printf("temp %.2fC, humdity %.2f%%, dewpt %.2f\r\n",temperature,humidity,aht_get_dew_point(temperature,humidity));
}


static TimerHandle_t s_tmr;
#endif
void aht_init(void ){
#if ENABLE_SENSOR_MODULE
	i2c_master_init();
	aht_setup();
	int tmr_id = 0;
    s_tmr = xTimerCreate("connTmr", (10000 / portTICK_RATE_MS),
                       pdTRUE, (void *)tmr_id, read_temperature);
    xTimerStart(s_tmr, portMAX_DELAY);
#endif
}


