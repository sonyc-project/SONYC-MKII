
#include "serial.h"
#include "usart.h"

#define MY_UART &huart5 // Not used by default

#ifndef STRING_BUF_SIZE
#define STRING_BUF_SIZE 128
#endif

// Note buffers are large to accomodate testing

#ifndef USB_IN_BUF_SIZE
#define USB_IN_BUF_SIZE 2048
#endif

#define INBUF_IDX usb_cdc_status.RxQueueBytes // Alias, to confuse people later

static char buffer[STRING_BUF_SIZE];
static uint8_t inbuf[USB_IN_BUF_SIZE];		// USB buffer
											// Using libc for outbuf
static uint8_t rx_pkt_buf[USB_CDC_PACKET_SIZE] __attribute__ ((aligned (32)));
static uint8_t tx_pkt_buf[USB_CDC_PACKET_SIZE] __attribute__ ((aligned (32)));

static usb_cdc_status_t usb_cdc_status;

// ST Driver Stuff
extern USBD_HandleTypeDef hUsbDeviceFS;
static int8_t my_cdc_rx_fs(uint8_t* pbuf, uint32_t *Len);
static int8_t my_cdc_init_fs(void);
static int8_t my_cdc_deinit_fs(void);
static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length);

USBD_CDC_ItfTypeDef USBD_Interface_fops_FS =
{
  my_cdc_init_fs,
  my_cdc_deinit_fs,
  CDC_Control_FS,
  my_cdc_rx_fs
};
// End ST Driver Stuff


static void reset_status(usb_cdc_status_t *x) {
	memset(x, 0, sizeof(usb_cdc_status_t)); // A simple zero'ing
}

static int is_usb_link_up(void) {
	return usb_cdc_status.is_connected;
}

// On going operation with TX buffer
static int is_usb_tx_not_empty(void) {
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
	if (hcdc->TxState != 0) return 1;
	else return 0;
}

char * get_usb_pkt_outbuf(void) {
	return (char *)tx_pkt_buf;
}

void debug_printf(const char * restrict fmt, ...) {
	HAL_StatusTypeDef ret;
	uint16_t sz;
	va_list argptr;

	va_start(argptr, fmt);
	vsnprintf(buffer, STRING_BUF_SIZE, fmt, argptr);
	va_end(argptr);

	sz = strnlen(buffer, STRING_BUF_SIZE);
	if (sz == 0) { __BKPT(); return; }

	ret = HAL_UART_Transmit(MY_UART, (uint8_t *)buffer, sz, 1000);
	if (ret != HAL_OK) __BKPT();
}

// Also available in status_t, but here a more lightweight version for polling
int usb_data_ready(void) {
	return INBUF_IDX;
}

// IRQ
// Called at USB connection time, at the CDC level (not LL)
static int8_t my_cdc_init_fs(void) {
	USBD_CDC_SetTxBuffer(&hUsbDeviceFS, tx_pkt_buf, 0);
	USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_pkt_buf);
	__disable_irq();
	reset_status(&usb_cdc_status);
	usb_cdc_status.is_connected = 1;
	__enable_irq();
	return USBD_OK;
}

// IRQ
// Called at USB disconnect
static int8_t my_cdc_deinit_fs(void) {
	reset_status(&usb_cdc_status);
	return USBD_OK;
}

// IRQ
// Called after RX
static int8_t my_cdc_rx_fs(uint8_t* pbuf, uint32_t *Len) {
	if (INBUF_IDX + (*Len) > sizeof(inbuf)) {
		__BKPT();
		INBUF_IDX = 0; // Have to start overwriting data...
		usb_cdc_status.usb_cdc_overrun++; // Flag overrun event
	}

	// Copy from packet buffer to input buffer
	memcpy(&inbuf[INBUF_IDX], rx_pkt_buf, *Len);
	INBUF_IDX += *Len;

	// Sets up the NEXT Rx
	USBD_CDC_SetRxBuffer(&hUsbDeviceFS, rx_pkt_buf);
	USBD_CDC_ReceivePacket(&hUsbDeviceFS);
	return USBD_OK;
}

static int8_t CDC_Control_FS(uint8_t cmd, uint8_t* pbuf, uint16_t length) {
	return USBD_OK;
}

// Writes a COPY of current status to given pointer
void usb_cdc_get_status(usb_cdc_status_t *x) {
	if (x == NULL) return;
	__disable_irq();
	memcpy(x, &usb_cdc_status, sizeof(usb_cdc_status_t));
	__enable_irq();
}

// Sleeps with WFI until a char comes in, instead of blocking busy-wait
char getchar_wfi(void) {
	while(INBUF_IDX == 0) { __WFI(); }
	return getchar();
}

uint8_t CDC_Transmit_FS(uint8_t* Buf, uint16_t Len) {
	uint8_t result = USBD_OK;
	USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef*)hUsbDeviceFS.pClassData;
	if (hcdc->TxState != 0){
		return USBD_BUSY;
	}
	USBD_CDC_SetTxBuffer(&hUsbDeviceFS, Buf, Len);
	result = USBD_CDC_TransmitPacket(&hUsbDeviceFS);
	return result;
}

// Kind of sloppy to be locked for most of it, but it is clean and not worth further investment
int _read(int handle, void *buf, int size) {
	int ret=0;
	// Don't care about CDC not connected case, simply means nothing to read
	if (INBUF_IDX == 0) return ret;
	__disable_irq();
	if (size >= INBUF_IDX) { // Return the whole input buffer
		memcpy(buf, inbuf, INBUF_IDX);
		ret = INBUF_IDX;
		INBUF_IDX = 0;
	}
	else { // Return a piece of the input buffer, shift reminder down
		memcpy(buf, inbuf, size);
		memcpy(inbuf, &inbuf[size], sizeof(inbuf)-size); // Does not actually overlap so no need memmove()
		INBUF_IDX -= size;
		ret = size;
	}
	usb_cdc_status.RxBytes += ret;
	__enable_irq();
	return ret;
}

// Blocking
int _write(int handle, char *buf, int size) {
	int ret;
	if (!is_usb_link_up()) return size; // CDC not connected, pretend it worked
	do { // TODO: ADD TIMEOUT
		ret = CDC_Transmit_FS( (uint8_t *)buf, size);
	} while(ret == USBD_BUSY);
	if (ret != USBD_OK) __BKPT();
	__disable_irq();
	usb_cdc_status.TxBytes += size;
	__enable_irq();
	while (is_usb_tx_not_empty()) { __WFI(); } // Wait until TX complete to avoid race on buffer
	return size;
}