
#include "serial.h"
#include "usart.h"

#ifndef STRING_BUF_SIZE
#define STRING_BUF_SIZE 128
#endif


static void usart2_send_bytes(uint8_t *x, uint16_t sz) {
	HAL_UART_Transmit(&huart2, x, sz, INT_MAX);
}

#ifndef STRING_BUF_SIZE
#define STRING_BUF_SIZE 128
#endif
static char buffer[STRING_BUF_SIZE]; // TX string buffer

void debug_printf(const char * restrict fmt, ...) {
	va_list argptr;

	va_start(argptr, fmt);
	vsniprintf(buffer, STRING_BUF_SIZE, fmt, argptr);
	va_end(argptr);

	usart2_send_bytes((uint8_t *)buffer, strnlen(buffer, STRING_BUF_SIZE));
}
