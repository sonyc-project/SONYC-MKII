#pragma once

void bms_rx(void);
void bms_transmit(uint8_t *buf, int len);

void bms_uart_irq_handler(void);

// in main
void do_uart_rx(uint8_t *read_buf, int sz);
