#pragma once

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <limits.h>
#include <unistd.h>

#include "usbd_cdc_if.h"

#include "main.h"

#define USB_CDC_PACKET_SIZE 64

// Singleton status structure
typedef struct {
	volatile int is_connected;
	volatile int usb_cdc_overrun;
	unsigned TxBytes;
	unsigned RxBytes;
	volatile unsigned RxQueueBytes;
} usb_cdc_status_t;

HAL_StatusTypeDef uart_is_tx_ready(void);
HAL_StatusTypeDef poll_until_uart_tx_ready(void);
HAL_StatusTypeDef uart_send_data(uint8_t *x, uint16_t sz);

// USB functions. Some cleanup needed.
char * get_usb_pkt_outbuf(void);
void usb_cdc_get_status(usb_cdc_status_t *x);
int usb_data_ready(void);

// Misc
char getchar_wfi(void);

void debug_printf(const char * restrict fmt, ...) __attribute__ ((format (printf, 1, 2)));
