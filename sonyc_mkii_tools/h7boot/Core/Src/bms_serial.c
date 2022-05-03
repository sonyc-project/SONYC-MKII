#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "usart.h"
#include "main.h"

#include "stm32h7xx_ll_usart.h"
#include "bms_serial.h"

#define BMS_TIMEOUT_MS 20
#define BMS_TIMEOUT_LONG_MS 1000
#define BMS_UART &huart2

#define BMS_MAX_FRAME_SIZE 1024

/*

Protocol: Sending signals RTS to Receiver CTS pin. Receiver turns on UART and mirrors the reply.

Comms are full-duplex.
The SENDER:
	-- Asserts RTS for the duration of the transmission
	-- Enables RX before asserting RTS
	-- Does not disable RX until CTS input is de-asserted
	-- Check and process input buffers after CTS is de-asserted

The RECEIVER:
	-- Enables RX upon its CTS input being asserted (the senders RTS)
	-- If NOT transmitting data of its own, de-assert RTS upon receiving first byte of frame
	-- If IS transmitting, hold RTS until its own transmission completes

Both sides agree upon a minimum buffer size (maximum frame size) of 1024 bytes.

*/

static uint8_t bms_rx_buf[BMS_MAX_FRAME_SIZE];	// I *think* this is a narrow enough case that 'volatile' is not needed
static volatile uint8_t bms_rx_bytes;			// I don't think this needs to be volatile either but meh

static bool active_transmit;
static bool rts_state;

static void bms_assert_rts(void) { rts_state = true;  HAL_GPIO_WritePin(UART2_RTS_GPIO_Port, UART2_RTS_Pin, GPIO_PIN_SET); }
static void bms_clear_rts(void)  { rts_state = false; HAL_GPIO_WritePin(UART2_RTS_GPIO_Port, UART2_RTS_Pin, GPIO_PIN_RESET); }

static int get_cts(void) { return HAL_GPIO_ReadPin(UART2_CTS_GPIO_Port, UART2_CTS_Pin); }

static int bms_wait_cts(int state, int timeout) {
	int ret;
	do {
		ret = get_cts();
		if (ret == state) return ret;
		HAL_Delay(1);
		timeout--;
	} while(ret != state && timeout > 0);
	return ret;
}

static void set_uart_rx_it(bool set) {
	UART_HandleTypeDef *huart = BMS_UART;
	if (set) {
		SET_BIT  (huart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);
	}
	else {
		CLEAR_BIT(huart->Instance->CR1, USART_CR1_RXNEIE_RXFNEIE);
	}
}

// May call bms_transmit() so we must empty the RX buffer before do_uart_rx()
static void process_rx_data(void) {
	uint8_t *buf;
	int saved = bms_rx_bytes;
	bms_rx_bytes = 0;
	buf = (uint8_t *)malloc(saved);
	if (buf == NULL) return;
	memcpy(buf, bms_rx_buf, saved);
	do_uart_rx(buf, saved);
	free(buf);
}

void bms_rx(void) {
	int cts;
	set_uart_rx_it(true); // Allow RX before we signal CTS

	cts = bms_wait_cts(1, BMS_TIMEOUT_MS); // Sanity check, was already checked once
	if (cts == 0) { __BKPT(); return; } // Didn't get CTS signal, abort

	bms_assert_rts(); // Singal we are ready to receive

	// TODO: Timeout
	while(1) {
		cts = get_cts();
		if (!cts) break;
		__WFE(); // Waiting for data...
	}
	// Sender de-asserted CTS so we are done
	// RTS already cleared in ISR
	if (rts_state == true) __BKPT(); // Sanity check
	set_uart_rx_it(false);
	if (bms_rx_bytes)
		process_rx_data();
}

void bms_transmit(uint8_t *buf, int len) {
	HAL_StatusTypeDef ret;
	int cts;

	active_transmit = true;
	set_uart_rx_it(true); // Allow RX before we signal CTS
	bms_assert_rts();
	cts = bms_wait_cts(1, BMS_TIMEOUT_MS);

	if (cts == 0) { __BKPT(); return; } // Didn't get CTS signal, abort

	ret = HAL_UART_Transmit(BMS_UART, buf, len, 1000);
	if (ret != HAL_OK) __BKPT();

	active_transmit = false;
	bms_clear_rts();
	// A little awkward at this point, the BMS may be sending us something
	// Wait until it de-asserts CTS, then check the buffer
	cts = bms_wait_cts(0, BMS_TIMEOUT_LONG_MS); // How to handle a fail here...
	set_uart_rx_it(false);
	if (bms_rx_bytes)
		process_rx_data(); // if any
}

void bms_uart_irq_handler(void) {
	UART_HandleTypeDef *huart = BMS_UART;

	uint8_t data = (uint8_t)(huart->Instance->RDR & (uint8_t)0xFF);
	if (bms_rx_bytes == 0 && !active_transmit) bms_clear_rts();
	bms_rx_buf[bms_rx_bytes++] = data;
}
