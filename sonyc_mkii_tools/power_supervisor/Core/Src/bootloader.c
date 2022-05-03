#ifdef I_AM_BOOTLOADER
#include <stdbool.h>

#include "main.h"
#include "spi.h"
#include "gpio.h"
#include "crc.h"
#include "usart.h"

/*

NOTE:
In linker file that this and other critical functions are kept in first 16 kB

--Bootloader does not use interrupts except SysTick
--Bootloader must avoid .data section
NPS 2020-05-11
*/

static void bootloader_SystemClock_Config(void);

static void init_bootloader_gpio(void) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};

	__HAL_RCC_GPIOA_CLK_ENABLE();
	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOD_CLK_ENABLE();

	/*Configure GPIO pin Output Level */
	HAL_GPIO_WritePin(GPIOA, nFLASH_WP_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOB, nFLASH_RST_Pin|nFLASH_CS_Pin, GPIO_PIN_RESET);
	HAL_GPIO_WritePin(GPIOB, nLED_Pin, GPIO_PIN_SET);

	GPIO_InitStruct.Pin = nFLASH_WP_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	GPIO_InitStruct.Pin =  nLED_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = nFLASH_RST_Pin|nFLASH_CS_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = nFLASH_RST_Pin|nFLASH_CS_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

	GPIO_InitStruct.Pin = PA0_BUTTON_Pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(PA0_BUTTON_GPIO_Port, &GPIO_InitStruct);

	__HAL_AFIO_REMAP_PD01_ENABLE();

	HAL_GPIO_WritePin(GPIOA, GPIO_PIN_11, GPIO_PIN_RESET); // Default LDO OFF
	GPIO_InitStruct.Pin = GPIO_PIN_11;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

	HAL_GPIO_WritePin(GPIOD, GPIO_PIN_2, GPIO_PIN_SET); // Default LTC3130 DISABLED
	GPIO_InitStruct.Pin = GPIO_PIN_2;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);
}

static void deinit_bootloader_gpio() {
	__HAL_RCC_GPIOA_FORCE_RESET();
	__HAL_RCC_GPIOB_FORCE_RESET();
	__HAL_RCC_GPIOD_FORCE_RESET();

	__HAL_RCC_GPIOA_RELEASE_RESET();
	__HAL_RCC_GPIOB_RELEASE_RESET();
	__HAL_RCC_GPIOD_RELEASE_RESET();

	// Do I need this?
	__HAL_RCC_GPIOA_CLK_DISABLE();
	__HAL_RCC_GPIOB_CLK_DISABLE();
	__HAL_RCC_GPIOD_CLK_DISABLE();
}

// Button logic: High --> No press --> No bootloader
static bool bootloader_check_button(void) {
	GPIO_PinState pin = HAL_GPIO_ReadPin(PA0_BUTTON_GPIO_Port, PA0_BUTTON_Pin);
	if (pin == GPIO_PIN_SET) return true;
	else return false;
}

extern int * __m_storage_start;
extern int * __m_storage_end;
extern int * __m_storage_size;

extern int * __m_prog_flash_start;
extern int * __m_prog_flash_end;
extern int * __m_prog_flash_size;

static HAL_StatusTypeDef erase_prog_flash(void) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	uint32_t pages = (uint32_t)&__m_prog_flash_size / FLASH_PAGE_SIZE;
	uint32_t PageError;
	HAL_StatusTypeDef ret;

	pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
	pEraseInit.Banks = FLASH_BANK_1;
	pEraseInit.PageAddress = (uint32_t) &__m_prog_flash_start;
	pEraseInit.NbPages = pages;

	HAL_FLASH_Unlock();
	ret = HAL_FLASHEx_Erase(&pEraseInit, &PageError);
	HAL_FLASH_Lock();
	return ret;
}

/*
static void erase_storage_flash(void) {
	FLASH_EraseInitTypeDef pEraseInit = {0};
	uint32_t pages = (uint32_t)&__m_storage_size / FLASH_PAGE_SIZE;
	uint32_t PageError;

	pEraseInit.TypeErase = FLASH_TYPEERASE_PAGES;
	pEraseInit.Banks = FLASH_BANK_1;
	pEraseInit.PageAddress = (uint32_t) &__m_storage_start;
	pEraseInit.NbPages = pages;

	HAL_FLASH_Unlock();
	HAL_FLASHEx_Erase(&pEraseInit, &PageError);
	HAL_FLASH_Lock();
}
*/

static HAL_StatusTypeDef copy_storage_to_prog(void) {
	int len = (int)&__m_storage_size; // bytes
	uint32_t addr = (uint32_t) &__m_prog_flash_start;
	uint64_t *data = (uint64_t *)&__m_storage_start;
	HAL_StatusTypeDef ret = HAL_OK;

	HAL_FLASH_Unlock();
	for(int i=0; i<len; i+=8, addr+=8, data++) {
		ret = HAL_FLASH_Program(FLASH_TYPEPROGRAM_DOUBLEWORD, addr, *data); // Doubleword --> 64-bit (8 bytes)
		if (ret != HAL_OK) break;
	}
	HAL_FLASH_Lock();
	return ret;
}

// This will NOT work until after HAL_Init() and bootloader_SystemClock_Config()
static void toggle_led(int time_ms) {
	HAL_GPIO_TogglePin(GPIOB, nLED_Pin);
	HAL_Delay(time_ms);
	HAL_GPIO_TogglePin(GPIOB, nLED_Pin);
	HAL_Delay(time_ms);
}

#define FIRST_WORD_CHECK 0x2000C000
#define FLASH_ERROR_DISTRESS_INTERVAL 2500
#define NO_PROG_DISTRESS_INTERVAL 1000
#define NO_STORAGE_DISTRESS_INTERVAL 250
#define SUCCESS_3_HAPPY_BEEPS_INTERVAL 100

static void flash_error_sad(void) {
	while (1) {
		HAL_GPIO_TogglePin(GPIOB, nLED_Pin);
		HAL_Delay(FLASH_ERROR_DISTRESS_INTERVAL);
		HAL_GPIO_TogglePin(GPIOB, nLED_Pin);
		HAL_Delay(FLASH_ERROR_DISTRESS_INTERVAL);
	}
}

typedef void (*pFunction)(void) __attribute__ ((noreturn));

void bootloader(void) {
	uint32_t *flash_word_check;
	HAL_StatusTypeDef ret;

	init_bootloader_gpio();

	if (bootloader_check_button()) {
		flash_word_check = (uint32_t *)&__m_prog_flash_start;
		if (*flash_word_check == FIRST_WORD_CHECK) {
			deinit_bootloader_gpio();
			// Cortex-M3 vector table is MSP followed by Reset
			pFunction main_app = (pFunction) ( *(volatile int *) ((int)0x08004000 + sizeof(int)) );
			main_app(); // Never returns
		}
		else {
			HAL_Init();
			bootloader_SystemClock_Config();
			// Above is so that the toggle works...
			while(1) toggle_led(NO_PROG_DISTRESS_INTERVAL); // Distress
		}
	}

	// We have work to do

	HAL_Init();
	bootloader_SystemClock_Config();
	//MX_CRC_Init();

	// Make sure we have something to program...
	flash_word_check = (uint32_t *)&__m_storage_start;
	if (*flash_word_check != FIRST_WORD_CHECK) {
		while(1) toggle_led(NO_STORAGE_DISTRESS_INTERVAL); // Distress
	}

	// It was my intention to use the external flash...
	// but then I realized how much excess internal flash we have
	// MX_SPI1_Init();

	ret = erase_prog_flash();
	if (ret != HAL_OK) flash_error_sad();
	ret = copy_storage_to_prog();
	if (ret != HAL_OK) flash_error_sad();

	toggle_led(SUCCESS_3_HAPPY_BEEPS_INTERVAL);
	toggle_led(SUCCESS_3_HAPPY_BEEPS_INTERVAL);
	toggle_led(SUCCESS_3_HAPPY_BEEPS_INTERVAL);

	while(1) __WFE(); // No return, user must manually reset
}

static void bootloader_SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI|RCC_OSCILLATORTYPE_LSE;
  RCC_OscInitStruct.LSEState = RCC_LSE_BYPASS;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT - 1;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB busses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK)
  {
    Error_Handler();
  }
}
#endif // I_AM_BOOTLOADER