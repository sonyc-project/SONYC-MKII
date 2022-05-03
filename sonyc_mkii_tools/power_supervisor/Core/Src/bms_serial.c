#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include "usart.h"
#include "main.h"
#include "frame_ops.h"
#include "serial.h"
#include "bms_serial.h"

//#include "stm32h7xx_ll_usart.h"
#include "stm32f1xx_ll_usart.h"

#define BMS_TIMEOUT_MS 10
#define BMS_TIMEOUT_LONG_MS 100
#define BMS_UART &huart1

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
static volatile int bms_rx_bytes;				// I don't think this need to be volatile either but meh
static volatile bool aborted_rx;				// Flag if anything weird happened in RX, we bail.

static bool active_transmit;

// Names are referenced with respect to H7 --> RTS is INPUT PIN on F1 and CTS is OUTPUT PIN on F1
// TODO: Fix names to be respective of the running system...
#define RTS_PORT 	H7_UART_RTS_GPIO_Port
#define RTS_PIN 	H7_UART_RTS_Pin
#define CTS_PORT	H7_UART_CTS_GPIO_Port
#define CTS_PIN		H7_UART_CTS_Pin

static void bms_assert_rts(void) { HAL_GPIO_WritePin(CTS_PORT, CTS_PIN, GPIO_PIN_SET); }
static void bms_clear_rts(void)  { HAL_GPIO_WritePin(CTS_PORT, CTS_PIN, GPIO_PIN_RESET); }

static GPIO_PinState get_cts(void)	{ return HAL_GPIO_ReadPin(RTS_PORT, RTS_PIN); }

static int bms_wait_cts(int state, int timeout) {
	int ret;
	do {
		ret = get_cts();
		if (timeout == 0) break;
		if (ret == state) break;
		HAL_Delay(1);
		timeout--;
	} while(1);
	return ret;
}

static int bms_wait_cts_abort(int state, int timeout) {
	int ret;
	do {
		ret = get_cts();
		if (timeout == 0) break;
		if (ret == state) break;
		if (aborted_rx)   break;
		HAL_Delay(1);
		timeout--;
	} while(1);
	return ret;
}

static void set_uart_rx_it(bool set) {
	UART_HandleTypeDef *huart = BMS_UART;
	if (set) {
		SET_BIT  (huart->Instance->CR1, USART_CR1_RXNEIE);
	}
	else {
		CLEAR_BIT(huart->Instance->CR1, USART_CR1_RXNEIE);
	}
}

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

static void abort_rx(UART_HandleTypeDef *huart) {
	CLEAR_BIT(huart->Instance->CR1, (USART_CR1_RXNEIE | USART_CR1_PEIE));
	CLEAR_BIT(huart->Instance->CR3, USART_CR3_EIE);
	__DSB(); // paranoid
	HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
	aborted_rx = true;
}

void bms_rx(void) {
	int cts;
	uint32_t start_tick;

	bms_rx_bytes = 0;
	aborted_rx = false;
	set_uart_rx_it(true); // Allow RX before we signal CTS

	cts = bms_wait_cts(1, BMS_TIMEOUT_MS); // Sanity check
	if (cts == 0) { aborted_rx = true; goto out; }

	bms_assert_rts(); // Singal we are ready to receive

	start_tick = get_rtc_tick(); // Timeout
	while(1) {
		__WFI(); // Waiting for RX interrupt to bring in data...

		cts = get_cts();
		if (cts == 0 || aborted_rx) break;

		if (get_rtc_tick() - start_tick >= 2) {
			aborted_rx = true;
			break; // time out
		}
	}
	if (!aborted_rx) HAL_Delay(1); // Small pause to make sure we get last byte

out:
	set_uart_rx_it(false);
	reset_rts_state();
	if (bms_rx_bytes && !aborted_rx)
		process_rx_data();
}

void bms_transmit(uint8_t *buf, int len) {
	HAL_StatusTypeDef ret;
	int cts;

	bms_rx_bytes = 0;
	aborted_rx = false;
	active_transmit = true;

	set_uart_rx_it(true); // Allow RX before we signal CTS
	bms_assert_rts();
	cts = bms_wait_cts(1, BMS_TIMEOUT_MS);

	if (cts == 0) { // Didn't get CTS response signal, abort
		bms_clear_rts();
		debug_printf("TX CTS TIMEOUT\r\n");
		goto out;
	}

	ret = HAL_UART_Transmit(BMS_UART, buf, len, 100);
	active_transmit = false;
	bms_clear_rts();

	// If the other side has not de-asserted CTS, wait a bit
	cts = bms_wait_cts_abort(0, BMS_TIMEOUT_LONG_MS);
	if (cts == 1) aborted_rx = true; // Other side never de-asserted, timeout fail
	if (!aborted_rx) HAL_Delay(1); // Small pause to make sure we get last byte

out:
	active_transmit = false;
	set_uart_rx_it(false);
	reset_rts_state();
	if (bms_rx_bytes & !aborted_rx)
		process_rx_data();
}

void bms_uart_irq_handler(void) {
	UART_HandleTypeDef *huart = BMS_UART;
	uint8_t data = (uint8_t)(huart->Instance->DR & (uint8_t)0xFF); // F1 is DR instead of RDR as on H7

	if (bms_rx_bytes == 0 && !active_transmit) bms_clear_rts(); // Done on first RX byte
	if (bms_rx_bytes == sizeof(bms_rx_buf)) {	// Error
		abort_rx(huart);
		return;
	}
	bms_rx_buf[bms_rx_bytes++] = data;
}
