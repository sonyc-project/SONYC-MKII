#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>
#include "main.h"
#include "frame_ops.h"
#include "bms_serial.h"
#include "serial_frame.h"
#include "bootloader.h"

// TODO: Duped with main.c
#define HELLO_STRING "SONYC Mel BMS Compiled " __DATE__ " " __TIME__ "\r\n"

// Not actually used...
typedef struct {
	//FILE *audio_file;
	unsigned audio_bytes_written;
	unsigned debug_bytes_written;
	unsigned data_bytes_written;
	unsigned boot_bytes_written;
	unsigned frame_count;
} mel_status_t;

// FLASH OPERATIONS --- MOVE ME

extern int * __m_storage_start;
extern int * __m_storage_end;
extern int * __m_storage_size;
extern int * __m_prog_flash_start;
extern int * __m_prog_flash_end;
extern int * __m_prog_flash_size;

static HAL_StatusTypeDef erase_storage_flash(void) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	uint32_t pages = (uint32_t)&__m_storage_size / FLASH_PAGE_SIZE;
	uint32_t PageError;
	HAL_StatusTypeDef ret;

	pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
	pEraseInit.Banks = FLASH_BANK_1;
	pEraseInit.PageAddress = (uint32_t) &__m_storage_start;
	pEraseInit.NbPages = pages;

	HAL_FLASH_Unlock();
	ret = HAL_FLASHEx_Erase(&pEraseInit, &PageError);
	HAL_FLASH_Lock();
	__DSB(); __ISB();
	return ret;
}

// END FLASH OPERATIONS --- MOVE ME

static void send_ack_reply(void) {
	uint8_t buf[FRAME_MIN_SIZE*2];
	int ret;
	ret = serial_frame_encode(NULL, 0, sizeof(buf), buf, DEST_BASE, FRAME_TYPE_ACK);
	bms_transmit(buf, ret);
}

// Error condition
// Additional info in debug string, if any
static void send_nack_reply(void) {
	uint8_t buf[FRAME_MIN_SIZE*2];
	int ret;
	ret = serial_frame_encode(NULL, 0, sizeof(buf), buf, DEST_BASE, FRAME_TYPE_NACK);
	bms_transmit(buf, ret);
}

static void hello_frame_handler(serial_frame_t *f, mel_status_t *status) {
	printf_frame(HELLO_STRING);
	send_ack_reply();
}

// Assumes Data is padded to % 8 bytes (e.g. 1 FLASH word)
#define FLASH_WORD_SIZE 8 // bytes, a double-word in this case
static void prog_helper(boot_cmd_packet_t *p, serial_frame_t *f) {
	HAL_StatusTypeDef ret;
	const int offset = sizeof(*p);
	uint64_t *data = (uint64_t *)(&f->buf[offset]);
	uint32_t addr = p->arg0;
	int bin_len = f->sz - offset;
	static int is_erased=0;
	uint32_t now;

	// There is a bug or something... not sure if this is safe...
	if (bin_len > 1024) bin_len = 1024;

	// if (bin_len % FLASH_WORD_SIZE != 0) {
		// printf_frame("Binary is sized %d but must be padded to mod 8\r\n", bin_len);
		// send_nack_reply();
		// return;
	// }

	if (!is_erased) {
		now = HAL_GetTick();
		ret = erase_storage_flash();
		if (ret == HAL_OK) {
			printf_frame("Erase operation took %lu ms... programming %d bytes...\r\n", HAL_GetTick()-now, bin_len);
		}
		else {
			printf_frame("Erase FAILED\r\n");
			send_nack_reply();
			return;
		}
		is_erased=1;
	}

	now = HAL_GetTick();
	HAL_FLASH_Unlock();
	while(bin_len) {
		ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, *data);
		data++;
		bin_len -= FLASH_WORD_SIZE;
		addr += FLASH_WORD_SIZE;
		if (ret != HAL_OK) {
			printf_frame("Program failed :(\r\n");
			send_nack_reply();
			goto out;
		}
	}
	printf_frame("Programming complete tooks %lu ms\r\n", HAL_GetTick()-now);
	send_ack_reply();
out:
	HAL_FLASH_Lock();
}

static void boot_frame_handler(serial_frame_t *f, mel_status_t *status) {
	boot_cmd_packet_t pkt;
	memcpy(&pkt, f->buf, sizeof(pkt));
	switch(pkt.cmd) {
		// case boot_cmd_hello: 	send_hello_reply(); 	break;
		// case boot_cmd_erase: 	erase_helper(&pkt); 	break;
		case boot_cmd_program:		prog_helper(&pkt, f); 	break;
		// case boot_cmd_boot:		boot_helper();			break;
		default: printf_frame("Ignoring cmd %d\r\n", pkt.cmd); send_nack_reply();
	}
	if (status != NULL)
		status->boot_bytes_written += f->sz;
}

static void handle_frame(serial_frame_t *f, mel_status_t *status) {
	if (f->dest != DEST_BMS) goto out;
	else switch(f->type) {
		case FRAME_TYPE_HELLO: hello_frame_handler(f, status); break;
		case FRAME_TYPE_BOOTLOADER_BIN: boot_frame_handler(f, status); break;
		default: break;
	}
out:
	if (status != NULL)
		status->frame_count++;
}

static bool parse_frame(serial_frame_t *f, mel_status_t *status) {
	if (f == NULL) { return false; }
	frame_flag_t flag = f->flag;
	if (f->err == NO_FRAME) return false;
	//if (f->err) printf("Frame Error %d\r\n", f->err);

	if (flag & CRC_ERROR) {
		// TODO
	}

	if (flag & FRAME_FOUND) {
		handle_frame(f, status);
		if (f->buf)
			free(f->buf);
		f->buf = NULL;
		f->sz = 0;
		return true;
	}
	return false;
}

void do_uart_rx(uint8_t *read_buf, int sz) {
	serial_frame_t f = {0};
	int i=0;
	int decode_ret;
	bool go = false;
	do {
		decode_ret = serial_frame_decode(&read_buf[i], sz-i, &f);
		if (decode_ret < 0) { break; }
		i += decode_ret;
		go = parse_frame(&f, NULL);
	} while (go);
}

void printf_frame(const char * restrict fmt, ...) {
	size_t needed, needed_frame;
	int ret;
	char *debug_string_buf=NULL, *debug_send_buf=NULL;
	va_list argptr;

	va_start(argptr, fmt);

	needed = vsnprintf(NULL, 0, fmt, argptr) + 1;
	debug_string_buf = (char *)malloc(needed);
	if (debug_string_buf == NULL) goto out;
	vsnprintf(debug_string_buf, needed, fmt, argptr);

	needed_frame = serial_frame_encode_count((uint8_t *)debug_string_buf, needed, DEST_BASE, FRAME_TYPE_DEBUG_STRING_BMS);
	debug_send_buf = (char *)malloc(needed_frame);
	if (debug_send_buf == NULL) goto out;
	ret = serial_frame_encode((uint8_t *)debug_string_buf, needed, needed_frame, (uint8_t *)debug_send_buf, DEST_BASE, FRAME_TYPE_DEBUG_STRING_BMS);

#ifdef _DEBUG
	if (ret != needed_frame) __BKPT(); // Sanity check
#endif

	//write(STDOUT_FILENO, debug_send_buf, ret);
	bms_transmit((uint8_t *)debug_send_buf, ret);

out:
	va_end(argptr);
	if (debug_string_buf) free(debug_string_buf);
	if (debug_send_buf) free(debug_send_buf);
	return;
}

void send_button_frame(void) {
	size_t needed_frame;
	uint8_t *buf;
	int ret;

	needed_frame = serial_frame_encode_count(NULL, 0, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
	buf = (uint8_t *)malloc(needed_frame);
	if (buf == NULL) return;
	ret = serial_frame_encode(NULL, 0, needed_frame, buf, DEST_H7, FRAME_TYPE_BOOTLOADER_BIN);
#ifdef _DEBUG
	if (ret != needed_frame) __BKPT();
#endif
	bms_transmit(buf, ret);

	free(buf);
}