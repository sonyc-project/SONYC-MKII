#pragma once

void bms_rx(void);
void bms_transmit(uint8_t *buf, int len);

void bms_uart_irq_handler(void);

uint32_t get_rtc_tick(void);
void reset_rts_state(void);
