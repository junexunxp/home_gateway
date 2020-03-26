#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sdkconfig.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#include "gw_sys_config.h"
#include "gw_macro.h"
#include "serial.h"
#if ENABLE_ZB_MODULE
#define ZBUART_RXBUFF               1024



void eSerial_Init(void)
{
#if ENABLE_ZIGBEE_MODULE
	uart_config_t uart_config = {
        .baud_rate = UART_BAUD_115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 122,
    };
    // Configure UART1 parameters
    uart_param_config(UART_NUM1, &uart_config);
    // Set UART1 pins(TX: IO4, RX: I05, RTS: IO18, CTS: IO19)
    uart_set_pin(UART_NUM1, UART1_TX_PIN, UART1_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    // Install UART driver (we don't need an event queue here)
    uart_driver_install(UART_NUM1, BUF_SIZE * 2, 0, 0, NULL, 0);
    uart_set_mode(UART_NUM1, UART_MODE_UART);
#endif
}

teSerial_Status eSerial_Read(uint8_t *data)
{
#if ENABLE_ZIGBEE_MODULE
	int len = uart_read_bytes(UART_NUM1, data, 1,portMAX_DELAY);

    if (len ) {
        return E_SERIAL_OK;
    }
#endif
    return E_SERIAL_NODATA;
}

void eSerial_WriteBuffer(uint8_t *data, uint8_t length)
{
#if ENABLE_ZIGBEE_MODULE
	uart_write_bytes(UART_NUM1, (char*)data, length);
#endif
}

#endif
