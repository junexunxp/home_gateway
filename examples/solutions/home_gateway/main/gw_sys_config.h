#ifndef SYS_CONFIG_H_
#define SYS_CONFIG_H_

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/uart.h"

#define ENABLE_ZIGBEE_MODULE 1
#define ENABLE_SENSOR_MODULE 1
#define ENABLE_FACTORY_RESET 0
#if ENABLE_SENSOR_MODULE
//I2C configure to drive sensors
#define I2C_MASTER_SCL_IO 23               /*!< gpio number for I2C master clock */
#define I2C_MASTER_SDA_IO 22               /*!< gpio number for I2C master data  */
#define I2C_MASTER_NUM 0 /*!< I2C port number for master dev */
#define I2C_MASTER_FREQ_HZ 100000        /*!< I2C master clock frequency */
#define I2C_MASTER_TX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */
#define I2C_MASTER_RX_BUF_DISABLE 0                           /*!< I2C master doesn't need buffer */
#define WRITE_BIT I2C_MASTER_WRITE              /*!< I2C master write */
#define READ_BIT I2C_MASTER_READ                /*!< I2C master read */
#define ACK_CHECK_EN 0x1                        /*!< I2C master will check ack from slave*/
#define ACK_CHECK_DIS 0x0                       /*!< I2C master will not check ack from slave */
#define ACK_VAL 0x0                             /*!< I2C ack value */
#define NACK_VAL 0x1                            /*!< I2C nack value */
#endif

#if ENABLE_ZIGBEE_MODULE
//UART config to drive zb gateway

/*!
 * @brief eZb_Uart_Init function
 */

#define UART_TAG         "Uart"
#define UART_NUM1        (UART_NUM_1)
#define BUF_SIZE         (100)
#define UART1_RX_PIN     (GPIO_NUM_4)
#define UART1_TX_PIN     (GPIO_NUM_5)
#define UART_BAUD_11520  (11520)
#define UART_BAUD_115200 (115200)
#define TOLERANCE        (0.02)    //baud rate error tolerance 2%.
//zb module reset pin
#define GPIO_OUTPUT_RESET_ZB    GPIO_NUM_21
#define GPIO_OUTPUT_RESET_ZB_PIN_SEL  (1ULL<<GPIO_OUTPUT_RESET_ZB)
#define ZB_MODULE_RESET()	gpio_set_level(GPIO_OUTPUT_RESET_ZB,0)
#define ZB_MODULE_ON()	gpio_set_level(GPIO_OUTPUT_RESET_ZB,1)

#endif


#if ENABLE_FACTORY_RESET
//ESP32 factory new reset pin enable
#define GPIO_OUTPUT_FACTORY_RESET   GPIO_NUM_12
#define GPIO_OUTPUT_FR_PIN_SEL  (1ULL<<GPIO_OUTPUT_FACTORY_RESET)\
#endif
#endif
#endif

