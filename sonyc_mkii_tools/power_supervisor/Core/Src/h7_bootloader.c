
// THIS IS ABORTED CODE. DELETE ME EVENTUALLY. NOT IN MAKEFILE.
#error "This is dead code"

#include "main.h"
#include "serial.h"

#define ACK 0x79
#define NACK 0x1F

// HAL_StatusTypeDef HAL_UART_Receive(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout)
// HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *huart, uint8_t *pData, uint16_t Size, uint32_t Timeout)

static void my_err(volatile HAL_StatusTypeDef ret) {
	__BKPT();
}

static int cmd_get(UART_HandleTypeDef *huart) {
	HAL_StatusTypeDef ret;
	uint8_t ack;
	uint8_t len;
	uint8_t bytes[] = { 0x00, 0xFF };
	uint8_t buf[16];

	// Send command
	ret = HAL_UART_Transmit(huart, bytes, sizeof(bytes), 100);
	if (ret != HAL_OK) my_err(ret);

	// Wait for (N)ACK
	ret = HAL_UART_Receive(huart, &ack, 1, 100);
	if (ret != HAL_OK) my_err(ret);

	if (ack == NACK) { debug_printf("GET cmd returned NACK\r\n"); return -1; }

	// Get len
	ret = HAL_UART_Receive(huart, &len, 1, 100);
	if (ret != HAL_OK) my_err(ret);
	len = len + 2;

	// Get response
	ret = HAL_UART_Receive(huart, buf, len, 100);
	if (ret != HAL_OK) my_err(ret);

	for(int i=0; i<sizeof(buf); i++) debug_printf("0x%X ", buf[i]);

	debug_printf("\r\n");
	return 0;
}

static int h7_init(UART_HandleTypeDef *huart) {
	HAL_StatusTypeDef ret;
	uint8_t byte = 0x7F;
	uint8_t ack;
	
	// char nate[] = "Nathan";
	// ret = HAL_UART_Transmit(huart, (uint8_t *)nate, sizeof(nate), 100);
	// if (ret != HAL_OK) my_err(ret);	
	//__BKPT();

	// Send init byte
	ret = HAL_UART_Transmit(huart, &byte, 1, 100);
	if (ret != HAL_OK) my_err(ret);
	
	// Get response
	ret = HAL_UART_Receive(huart, &ack, 1, 1000);
	if (ret != HAL_OK) my_err(ret);
	
	if (ack == NACK) { debug_printf("GET cmd returned NACK\r\n"); return -1; }
	else { debug_printf("ACK from H7\r\n"); return 0; }
}

void h7_st_bootloader(UART_HandleTypeDef *huart) {
	h7_init(huart);
	cmd_get(huart);
	debug_printf("done\r\n");
}
