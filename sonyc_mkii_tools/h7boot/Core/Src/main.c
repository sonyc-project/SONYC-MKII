#include "main.h"
#include "crc.h"
#include "lptim.h"
#include "quadspi.h"
#include "usart.h"
#include "usb_device.h"
#include "gpio.h"
#include "fmc.h"

// TODO: Remove libc stuff (malloc) or add option. Check with _sbrk()
#include <stdio.h>
#include <stdbool.h>

#include "serial.h"
#include "serial_frame.h"
#include "message.h" // nanopb message handler
#include "memory_map.h" // Defines flash memory regions etc.
#include "bootloader.h"
#include "bms_serial.h"
#include "network_id.h"

#define BOOTLOADER_MAGIC_WORD 0xEE33BB22 // Forces bootloader
#define BOOTLOADER_OTHER_WORD 0xAABBCCEE // Forces boot to app
#define BOOTLOADER_MAGIC_ADDR 0x38000000 // first word of D3

#define HELLO_STRING "H7 Bootloader Compiled " __DATE__ " " __TIME__ "\r\n"

// We are running out of ITCM and DTCM so normally off...
//#define ENABLE_CACHE

#define MY_FLASH_VOLTAGE_RANGE FLASH_VOLTAGE_RANGE_4 // fastest 256-bit ops for >= 1.8v
#define FLASH_VERBOSE
//#define USE_STDIO_BUFFER

//#define USE_UART5_DEBUG
#ifndef USE_UART5_DEBUG
#define debug_printf(...) ((void)0)
#endif

typedef struct {
	//FILE *audio_file;
	unsigned audio_bytes_written;
	unsigned debug_bytes_written;
	unsigned data_bytes_written;
	unsigned boot_bytes_written;
	unsigned frame_count;
} mel_status_t;

void SystemClock_Config(void);

// For relocating vector table
extern void * _end_isr_vector;
extern void * g_pfnVectors;

// Bootloader core stuff
typedef void (*pFunction)(void) __attribute__ ((noreturn));
static void start_app_at(int vec);
static inline void app(void);

static void erase_program(void) __attribute__ ((unused));
static void erase_region(const h7_flash_region_t *x) __attribute__ ((unused));
static void wipe_flash(void)  __attribute__ ((unused));
static void nuke_flash(void) __attribute__ ((unused));

static void set_stdio_bufs(void) {
	int ret;
	char *buf = get_usb_pkt_outbuf();
	if (buf == NULL) __BKPT();
#ifdef USE_STDIO_BUFFER
	ret = setvbuf(stdout, buf, _IOFBF, USB_CDC_PACKET_SIZE); // Full buffering, single packet size (64 bytes)
#else
	ret = setvbuf(stdout, buf, _IONBF, USB_CDC_PACKET_SIZE); // NO buffering, single packet size (64 bytes)
#endif
	if (ret != 0) __BKPT();
}

// Not used, Flash interrupts explictly disabled
void HAL_FLASH_EndOfOperationCallback(uint32_t ReturnValue) { }

// Not used, Flash interrupts explictly disabled
void HAL_FLASH_OperationErrorCallback(uint32_t ReturnValue) { }

#define FLASH_ERROR_CHECK(x) if (x != HAL_OK) flash_error(__func__)
static void flash_error(const char *s) {
	__BKPT();
#ifdef FLASH_VERBOSE
	debug_printf("%s(): Fatal Flash operation error...\r\n", s);
#endif
	while(1) __WFI();
}

static void set_magic_word(void) {
	volatile uint32_t *magic = (volatile uint32_t *)BOOTLOADER_MAGIC_ADDR;
	*magic = BOOTLOADER_MAGIC_WORD;
}

// Also is "other" word
static void clear_magic_word(void) {
	volatile uint32_t *magic = (volatile uint32_t *)BOOTLOADER_MAGIC_ADDR;
	*magic = 0;
}

// Magic word signals stop and wait
// NEW: if target address is erased, always stay in bootloader mode
static bool check_magic_word(void) {
	uint32_t magic = *((volatile uint32_t *)BOOTLOADER_MAGIC_ADDR);
	uint32_t *APP_DATA = (uint32_t *)APPLICATION_START_ADDR;
	if (*APP_DATA == 0xFFFFFFFF) return true; // If app is erased (all 1s) then always bootloader
	if (magic == BOOTLOADER_MAGIC_WORD)
		return true;
	else
		return false;
}

// Other word signals go straight to app after reset
static void set_other_word(void) {
	volatile uint32_t *magic = (volatile uint32_t *)BOOTLOADER_MAGIC_ADDR;
	*magic = BOOTLOADER_OTHER_WORD;
}

// Other word signals go straight to app after reset
static bool check_other_word(void) {
	uint32_t magic = *((volatile uint32_t *)BOOTLOADER_MAGIC_ADDR);
	if (magic == BOOTLOADER_OTHER_WORD)
		return true;
	else
		return false;
}

static void erase_region(const h7_flash_region_t *x) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	HAL_StatusTypeDef ret;
	uint32_t SectorError;
	uint32_t bank, sector, len;

	bank = x->bank;
	sector = x->sector;
	len = x->len;

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Starting...\r\n", __func__);
#endif

	if (bank > MAX_BANKS || !bank) flash_error(__func__); // 2 banks numbered 1 and 2
	if (sector >= MAX_SECTORS) flash_error(__func__); // 0-7 valid
	if (len + sector > MAX_SECTORS || !len) flash_error(__func__); // 8 sectors per bank

	// Setup
	pEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
	pEraseInit.Banks = bank;
	pEraseInit.Sector = sector;
	pEraseInit.NbSectors = len;
	pEraseInit.VoltageRange = MY_FLASH_VOLTAGE_RANGE; // >= 1.8v so we're good to 4

	ret = HAL_FLASH_Unlock(); FLASH_ERROR_CHECK(ret);
	ret = HAL_FLASHEx_Erase(&pEraseInit, &SectorError); FLASH_ERROR_CHECK(ret);
	ret = HAL_FLASH_Lock(); FLASH_ERROR_CHECK(ret);

	if (HAL_FLASH_GetError() != HAL_FLASH_ERROR_NONE) { flash_error(__func__); }

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Complete\r\n", __func__);
#endif

	// Flush pipeline
	__DSB(); __ISB();
}

// Helpers
#define erase_csharp()		erase_region(&FLASH_CSHARP_REGION)
#define erase_bootloader()	erase_region(&FLASH_BOOTLOADER_REGION)
static void erase_program(void) {
	erase_region(&FLASH_PROGRAM_REGION_BANK1);
	erase_region(&FLASH_PROGRAM_REGION_BANK2);
}

// Erase everything EXCEPT the 0th sector (thus preserving the bootloader)
// Only different than erase_region() in that it uses mass_erase function
static void wipe_flash(void) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	HAL_StatusTypeDef ret;
	uint32_t SectorError;

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Starting...\r\n", __func__);
#endif

	// Setup
	pEraseInit.TypeErase = FLASH_TYPEERASE_SECTORS;
	pEraseInit.Banks = FLASH_BANK_1;
	pEraseInit.Sector = FLASH_SECTOR_1; // Ignore 0th sector
	pEraseInit.NbSectors = MAX_SECTORS-1; // Wipe 7 sectors (8-1)
	pEraseInit.VoltageRange = MY_FLASH_VOLTAGE_RANGE; // >= 1.8v so we're good to 4

	ret = HAL_FLASH_Unlock(); FLASH_ERROR_CHECK(ret);
	ret = HAL_FLASHEx_Erase(&pEraseInit, &SectorError); FLASH_ERROR_CHECK(ret);

	// Bank2 mass erase
	pEraseInit.TypeErase = FLASH_TYPEERASE_MASSERASE;
	pEraseInit.Banks = FLASH_BANK_2;
	ret = HAL_FLASHEx_Erase(&pEraseInit, &SectorError); FLASH_ERROR_CHECK(ret);

	ret = HAL_FLASH_Lock(); FLASH_ERROR_CHECK(ret);

	if (HAL_FLASH_GetError() != HAL_FLASH_ERROR_NONE) {
		flash_error(__func__);
	}

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Complete\r\n", __func__);
#endif

	// Flush pipeline
	__DSB(); __ISB();
}

// Erase entire Flash memory (2 MiB)
// Note that this includes the bootloader itself...
// So must write a new bootloader before reboot or bricked until JTAG
// Only different than erase_region() in that it uses mass_erase function
static void nuke_flash(void) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	HAL_StatusTypeDef ret;
	uint32_t SectorError; // not used

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Starting...\r\n", __func__);
#endif

	// Setup
	pEraseInit.TypeErase = FLASH_TYPEERASE_MASSERASE;
	pEraseInit.Banks = FLASH_BANK_1; // MUST DO 1 AND 2 IN DIFFERENT OPERATIONS
	pEraseInit.VoltageRange = MY_FLASH_VOLTAGE_RANGE; // >= 1.8v so we're good to 4

	ret = HAL_FLASH_Unlock(); FLASH_ERROR_CHECK(ret);
	ret = HAL_FLASHEx_Erase(&pEraseInit, &SectorError); FLASH_ERROR_CHECK(ret);

	// Other bank
	pEraseInit.Banks = FLASH_BANK_2;
	ret = HAL_FLASHEx_Erase(&pEraseInit, &SectorError); FLASH_ERROR_CHECK(ret);

	ret = HAL_FLASH_Lock(); FLASH_ERROR_CHECK(ret);

	if (HAL_FLASH_GetError() != HAL_FLASH_ERROR_NONE) {
		flash_error(__func__);
	}

#ifdef FLASH_VERBOSE
	debug_printf("%s(): Complete\r\n", __func__);
#endif

	// Flush pipeline
	__DSB(); __ISB();
}

// static void frame_default_handler(serial_frame_t *f) {
	// Error_Handler();
// }

// Signals action complete
static void send_ack_reply(void) {
	uint8_t buf[FRAME_MIN_SIZE*2];
	int ret;
	ret = serial_frame_encode(NULL, 0, sizeof(buf), buf, DEST_BASE, FRAME_TYPE_ACK);
	write(STDOUT_FILENO, buf, ret);
}

// Error condition
// Additional info in debug string, if any
static void send_nack_reply(void) __attribute__ ((unused));
static void send_nack_reply(void) {
	uint8_t buf[FRAME_MIN_SIZE*2];
	int ret;
	ret = serial_frame_encode(NULL, 0, sizeof(buf), buf, DEST_BASE, FRAME_TYPE_NACK);
	write(STDOUT_FILENO, buf, ret);
}

static void printf_frame(const char * restrict fmt, ...) {
	size_t needed, needed_frame;
	int ret;
	char *debug_string_buf=NULL, *debug_send_buf=NULL;
	va_list argptr;

	va_start(argptr, fmt);

	needed = vsnprintf(NULL, 0, fmt, argptr) + 1;
	debug_string_buf = (char *)malloc(needed);
	if (debug_string_buf == NULL) goto out;
	vsnprintf(debug_string_buf, needed, fmt, argptr);

	needed_frame = serial_frame_encode_count((uint8_t *)debug_string_buf, needed, DEST_BASE, FRAME_TYPE_DEBUG_STRING);
	debug_send_buf = (char *)malloc(needed_frame);
	if (debug_send_buf == NULL) goto out;
	ret = serial_frame_encode((uint8_t *)debug_string_buf, needed, needed_frame, (uint8_t *)debug_send_buf, DEST_BASE, FRAME_TYPE_DEBUG_STRING);

	if (ret != needed_frame) __BKPT(); // Sanity check

	write(STDOUT_FILENO, debug_send_buf, ret);

out:
	va_end(argptr);
	if (debug_string_buf) free(debug_string_buf);
	if (debug_send_buf) free(debug_send_buf);
	return;
}

static void send_hello_reply(void) {
	const uint32_t *uid = (const uint32_t *)get_cpu_uid();
	uint16_t uid_hash = get_cpu_uid_hash16();

	printf_frame(HELLO_STRING);

	if (get_cpu_uid_len() == 12)
		printf_frame("CPU UID: 0x%.8X%.8X%.8X\r\n", uid[0], uid[1], uid[2]);
	else
		printf_frame("ERROR: Unexpected UID size\r\n");

	printf_frame("Device Network ID %u (0x%.4X)\r\n", uid_hash, uid_hash);
	send_ack_reply();
}

static void erase_helper(boot_cmd_packet_t *p) {
	uint64_t start_ms = lptim_get_ms();
	uint64_t stop_ms;
	uint32_t diff_ms;
	int start = p->arg0;
	int end	  = p->arg1;

	// Input checks
	if (start > end || start < 0 || end < 0 || start > 15 || end > 15) {
		printf_frame("BAD ERASE ARGS %d %d\r\n", start, end);
		send_nack_reply();
		return;
	}

	// Special mass erase case
	if (start == FIRST_SECTOR && end == LAST_SECTOR) {
		nuke_flash();
		goto out;
	}

	// Bank 1 of 2
	if (start < MAX_SECTORS) { // MAX_SECTORS is per bank
		int len;
		if (end < MAX_SECTORS)
			len = end - start + 1;
		else
			len = MAX_SECTORS - start;
		h7_flash_region_t x = {1,start,len};
		erase_region(&x);
	}

	// Bank 2 of 2
	if (end >= MAX_SECTORS) {
		if (start <= MAX_SECTORS) start = 0;
		else start = start % MAX_SECTORS;

		end   = end   % MAX_SECTORS;
		int len = end - start + 1;
		h7_flash_region_t x = {2,start,len};
		erase_region(&x);
	}
out:
	stop_ms = lptim_get_ms();
	diff_ms = (stop_ms - start_ms)&0xFFFFFFFF;
	printf_frame("Erase operation completed in %d ms\r\n", diff_ms);
	send_ack_reply();
}

// Assumes Data is padded to % 32 bytes (e.g. 1 FLASH word)
#define FLASH_WORD_SIZE 32 // bytes
static void prog_helper(boot_cmd_packet_t *p, serial_frame_t *f) {
	HAL_StatusTypeDef ret;
	static uint64_t start_time;
	const int offset = sizeof(*p);
	uint32_t *data = (uint32_t *)(&f->buf[offset]);
	uint32_t addr = p->arg0;
	int bin_len = f->sz - offset;

	if (start_time == 0)
		start_time = lptim_get_ms();

	if (bin_len % FLASH_WORD_SIZE != 0) {
		printf_frame("Binary is sized %d but must be padded to mod 32\r\n", bin_len);
		send_nack_reply();
		return;
	}

	debug_printf("Programming %d bytes at %p\r\n", bin_len, (void *)addr);

	HAL_FLASH_Unlock(); __DMB();
	while(bin_len) {
		ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, addr, (uint32_t)data);
		data += FLASH_WORD_SIZE/sizeof(data); // note pointer
		bin_len -= FLASH_WORD_SIZE;
		addr += FLASH_WORD_SIZE;
		if (ret != HAL_OK) {
			send_nack_reply();
			goto out;
		}
	}
out:
	HAL_FLASH_Lock();
	if (p->arg1 == 1) {
		uint64_t end_time = lptim_get_ms();
		uint32_t diff_time = (end_time - start_time)&0xFFFFFFFF;
		printf_frame("Program operation completed in %lu ms\r\n", diff_time);
		start_time = 0;
	}
	send_ack_reply();
}

static void boot_helper(void) {
	printf_frame("Reset and booting to application at %p...\r\n", (void *)APPLICATION_START_ADDR);
	send_ack_reply();
	set_other_word();
	HAL_Delay(10); // Allow USB time to flush
	NVIC_SystemReset();
}

static void boot_frame_handler(serial_frame_t *f, mel_status_t *status) {
	boot_cmd_packet_t pkt;

	memcpy(&pkt, f->buf, sizeof(pkt));

	switch(pkt.cmd) {
		case boot_cmd_hello: 	send_hello_reply(); 	break;
		case boot_cmd_erase: 	erase_helper(&pkt); 	break;
		case boot_cmd_program:	prog_helper(&pkt, f); 	break;
		case boot_cmd_boot:		boot_helper();			break;
		default: set_magic_word();
	}

	status->boot_bytes_written += f->sz;
}

static void forward_frame_to_bms(serial_frame_t *f) {
	unsigned bytes = sf_encode_from_struct_count(f);
	uint8_t *forward_buf = (uint8_t *)malloc(bytes);
	sf_encode_from_struct(f, forward_buf, bytes);
	bms_transmit(forward_buf, bytes);
	free(forward_buf);
}

static void forward_frame_to_base(serial_frame_t *f) {
	unsigned bytes = sf_encode_from_struct_count(f);
	uint8_t *forward_buf = (uint8_t *)malloc(bytes);
	sf_encode_from_struct(f, forward_buf, bytes);
	write(STDOUT_FILENO, forward_buf, bytes);
	free(forward_buf);
}

static void handle_frame(serial_frame_t *f, mel_status_t *status) {
	if (f->dest == DEST_BMS) forward_frame_to_bms(f);
	else if (f->dest == DEST_BASE) forward_frame_to_base(f);
	else switch(f->type) {
		// case FRAME_TYPE_DEBUG_STRING: debug_frame_handler(f, status); break;
		// case FRAME_TYPE_DATA_STRING: data_string_frame_handler(f, status); break;
		// case FRAME_TYPE_BIN_AUDIO: audio_frame_handler(f, status); break;
		// case FRAME_TYPE_HELLO: fprintf(stderr,"Got Hello\r\n"); break;
		case FRAME_TYPE_BOOTLOADER_BIN: boot_frame_handler(f, status); break;
		default: debug_printf("ERROR: Unknown frame type %lu\r\n", f->type);
	}
	if (status != NULL)
		status->frame_count++;
}

#define CHECK_FLAG(x) {if (flag & x) printf("Flag: " #x "\r\n");}
static bool parse_frame(serial_frame_t *f, mel_status_t *status) {
	if (f == NULL) { debug_printf("NULL!!!\r\n"); __BKPT(); return false; }
	frame_flag_t flag = f->flag;
	if (f->err == NO_FRAME) return false;
	if (f->err) printf("Frame Error %d\r\n", f->err);

	CHECK_FLAG(NONE_FLAG);
	//CHECK_FLAG(FRAME_FOUND);
	//CHECK_FLAG(PARTIAL);
	//CHECK_FLAG(NO_PAYLOAD);
	CHECK_FLAG(CRC_ERROR);

	if (flag & CRC_ERROR) {

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
		if (decode_ret < 0) { __BKPT(); break; }
		i += decode_ret;
		go = parse_frame(&f, NULL);
	} while (go);
}

static void do_usb_rx(void) {
	static uint8_t read_buf[FRAME_MAX_SIZE];
	static mel_status_t status;
	serial_frame_t f = {0};
	ssize_t ret = read(STDIN_FILENO, read_buf, sizeof(read_buf));
	int decode_ret;
	bool go = false;
	int i=0;

	if (ret < 0) { __BKPT(); return; }

	do {
		decode_ret = serial_frame_decode(&read_buf[i], ret-i, &f);
		if (decode_ret < 0) { __BKPT(); break; }
		i += decode_ret;
		go = parse_frame(&f, &status);
	} while (go);
}


static void start_app_at(int vec) {
	clear_magic_word();
	// Cortex-M3 vector table is MSP followed by Reset
	pFunction main_app = (pFunction) ( *(volatile int *) (vec + sizeof(int)) );
	main_app(); // Never returns
}

void * serial_frame_malloc(size_t size) {
	return malloc(size);
}

static void reboot_then_app(void) {
	set_other_word();
	NVIC_SystemReset();
}

static void led_dance(void) {
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin); HAL_Delay(50);
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin); HAL_Delay(50);
	HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin); HAL_Delay(50);
	HAL_GPIO_TogglePin(LED1_GPIO_Port, LED1_Pin); HAL_Delay(50);
	HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin); HAL_Delay(50);
	HAL_GPIO_TogglePin(LED2_GPIO_Port, LED2_Pin);
}

static void led_boot(void) {
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin); HAL_Delay(100);
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin); HAL_Delay(100);
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin); HAL_Delay(100);
	HAL_GPIO_TogglePin(LED0_GPIO_Port, LED0_Pin);
}

 // Default, first flash page after bootloader
static inline void app(void) { start_app_at(APPLICATION_START_ADDR); }

int main(void)
{
	if (check_other_word()) app(); // Signal after reboot to jump straight to app

	// Relocate vector table to ITCM
	uint32_t isr_len = (&_end_isr_vector - &g_pfnVectors) * sizeof(int); // hack around pointer arithmetic
	void *itcm_start = (void *)0;
	memcpy(itcm_start, &g_pfnVectors, isr_len);
	SCB->VTOR = 0;
	__DSB(); __ISB();
	__enable_irq();

#ifdef ENABLE_CACHE
	SCB_EnableICache();
	SCB_EnableDCache();
#endif

	HAL_Init();

	SystemClock_Config();
	MX_LPTIM_Init(); // Do early for LPTIM can be HAL_Delay (NYI)
	HAL_Delay(250); // Prevent startup shoot-thru

	MX_GPIO_Init();
	//MX_FMC_Init();

	// TODO: Check for valid app

	set_stdio_bufs();

	// Do heavier init once its clear we are doing some work
	MX_USART2_UART_Init();
#ifdef USE_UART5_DEBUG
	MX_UART5_Init();
#endif
	// MX_CRC_Init();
	// MX_QUADSPI_Init();

	uint32_t timeout_ms = 10000;
	uint32_t now = HAL_GetTick();

	// Wait timeout_ms for a button press to come in for emergency bootloader
	led_boot();
	while(HAL_GetTick() - now < timeout_ms) {
		if ( HAL_GPIO_ReadPin(UART2_CTS_GPIO_Port, UART2_CTS_Pin) == GPIO_PIN_SET ) {
			bms_rx();
			if (check_magic_word()) break;
		}
		__WFE(); // Button and SysTick will drive this
	}

	// If magic word is present, do the bootloader
	if ( !check_magic_word() ) reboot_then_app();
	else clear_magic_word(); // Reset the state
	led_dance(); // to indicate

	// If we are going to bootloader, init USB
	MX_USB_DEVICE_Init();
	// Keep USB active in WFI
	__HAL_RCC_USB2_OTG_FS_CLK_SLEEP_ENABLE();
	__HAL_RCC_USB2_OTG_FS_ULPI_CLK_SLEEP_DISABLE();

	HAL_EnableDBGSleepMode();
	//HAL_SuspendTick(); // leave on to use HAL_Delay() for now. Maybe fixup the LPTIM instead of SysTick...
	//getchar_wfi(); // Press any key to start
	debug_printf(HELLO_STRING);

	// Main RX loop
	while (1) {
		// USB
		if ( usb_data_ready() )
			do_usb_rx();

		// Signal from BMS
		if ( HAL_GPIO_ReadPin(UART2_CTS_GPIO_Port, UART2_CTS_Pin) == GPIO_PIN_SET )
			bms_rx();

		__WFE(); // Wait for next round
	}
}

// 96 MHz SYSCLK 96 MHz HCLK VOS3
void SystemClock_Config(void)
{
	RCC_OscInitTypeDef RCC_OscInitStruct = {0};
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

	HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

	__HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE3);

	while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

	RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE|RCC_OSCILLATORTYPE_LSE;
	RCC_OscInitStruct.HSEState = RCC_HSE_ON;
	RCC_OscInitStruct.LSEState = RCC_LSE_BYPASS;
	RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
	RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
	RCC_OscInitStruct.PLL.PLLM = 1;
	RCC_OscInitStruct.PLL.PLLN = 23;
	RCC_OscInitStruct.PLL.PLLP = 2;
	RCC_OscInitStruct.PLL.PLLQ = 4;
	RCC_OscInitStruct.PLL.PLLR = 128;
	RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
	RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOMEDIUM;
	RCC_OscInitStruct.PLL.PLLFRACN = 3584;
	if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
	{
	Error_Handler();
	}

	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
							  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
							  |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
	RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV16;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV16;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV16;
	RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV16;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
	{
	Error_Handler();
	}
	PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_USART2|RCC_PERIPHCLK_UART5
							  |RCC_PERIPHCLK_USB|RCC_PERIPHCLK_LPTIM1
							  |RCC_PERIPHCLK_QSPI|RCC_PERIPHCLK_FMC;
	PeriphClkInitStruct.FmcClockSelection = RCC_FMCCLKSOURCE_D1HCLK;
	PeriphClkInitStruct.QspiClockSelection = RCC_QSPICLKSOURCE_D1HCLK;
	PeriphClkInitStruct.Usart234578ClockSelection = RCC_USART234578CLKSOURCE_D2PCLK1;
	PeriphClkInitStruct.UsbClockSelection = RCC_USBCLKSOURCE_PLL;
	PeriphClkInitStruct.Lptim1ClockSelection = RCC_LPTIM1CLKSOURCE_LSE;
	if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
	{
	Error_Handler();
	}

	HAL_PWREx_EnableUSBVoltageDetector();
}

void Error_Handler(void) {
	__BKPT();
}

#ifdef  USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: debug_printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
