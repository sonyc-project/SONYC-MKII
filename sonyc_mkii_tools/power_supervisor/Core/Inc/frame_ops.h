#pragma once

void printf_frame(const char * restrict fmt, ...) __attribute__ ((format (printf, 1, 2)));

void send_button_frame(void);
void do_uart_rx(uint8_t *read_buf, int sz);