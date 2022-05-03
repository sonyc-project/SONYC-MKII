#include "lptim.h"

static uint32_t rtc_counter32; // Upper 32-bits of counter
static uint64_t time64; // last time

#define LSE_HZ 32768

// For changing LPTIM
// Note: MX_LPTIM_Init() changes are manual, and must also change stm32h7xx_it.c
// The LPTIMs do *not* have the same Init settings. Be careful. I think.
LPTIM_HandleTypeDef hlptim1; // cannot be static, linked to interrupt
#define MY_HLPTIM &hlptim1 // Pointer format used outside of init
#define MY_INSTANCE LPTIM1
#define LPTIM_IRQn LPTIM1_IRQn
#define CLK_EN		__HAL_RCC_LPTIM1_CLK_ENABLE
#define CLK_DIS		__HAL_RCC_LPTIM1_CLK_DISABLE

static int start_lptim(void);
static uint64_t get_counter64(void);

void MX_LPTIM_Init(void)
{
	hlptim1.Instance = MY_INSTANCE;
	hlptim1.Init.Clock.Source = LPTIM_CLOCKSOURCE_APBCLOCK_LPOSC;
	hlptim1.Init.Clock.Prescaler = LPTIM_PRESCALER_DIV1;
	hlptim1.Init.Trigger.Source = LPTIM_TRIGSOURCE_SOFTWARE;
	hlptim1.Init.OutputPolarity = LPTIM_OUTPUTPOLARITY_HIGH;
	hlptim1.Init.UpdateMode = LPTIM_UPDATE_IMMEDIATE;
	hlptim1.Init.CounterSource = LPTIM_COUNTERSOURCE_INTERNAL;
	hlptim1.Init.Input1Source = LPTIM_INPUT1SOURCE_GPIO;
	hlptim1.Init.Input2Source = LPTIM_INPUT2SOURCE_GPIO;

	if (HAL_LPTIM_Init(MY_HLPTIM) != HAL_OK) {
		Error_Handler();
	}
	start_lptim();
}

// Returns global time in 1 us ticks
uint64_t lptim_get_us(void) {
	return get_counter64() * 1000000 / LSE_HZ;
}

// Returns global time in 1 ms ticks
uint32_t lptim_get_ms(void) {
	uint64_t ret = get_counter64() * 1000 / LSE_HZ;
	return ret & 0xFFFFFFFF;
}

static uint64_t get_counter64(void) {
	uint32_t read, read2;
	uint64_t ret;
	unsigned timeout = 3;

	while(timeout) {
		__disable_irq();
		read = HAL_LPTIM_ReadCounter(MY_HLPTIM);
		ret = (((uint64_t)rtc_counter32)<<16) + read;
		__enable_irq();
		__NOP();
		read2 = HAL_LPTIM_ReadCounter(MY_HLPTIM);
		if (read <= read2) break;
		else timeout--; // read2 < read case, inconsistent, do it again
	}
	if (timeout == 0) __BKPT();

	// Sanity check
	if (ret < time64) {
		__BKPT();
		ret = time64; // monotonic time only
	} else {
		time64 = ret;
	}
	return ret;
}

static int start_lptim(void) {
	HAL_StatusTypeDef ret;
	ret = HAL_LPTIM_Counter_Start_IT(MY_HLPTIM, 0xFFFF);
	if (ret != HAL_OK) __BKPT();
	return 0;
}

// Overflow interrupt
void HAL_LPTIM_AutoReloadMatchCallback(LPTIM_HandleTypeDef *hlptim) {
	rtc_counter32++;
	__DMB();
}

void HAL_LPTIM_MspInit(LPTIM_HandleTypeDef* lptimHandle)
{
  if(lptimHandle->Instance==MY_INSTANCE)
  {
    CLK_EN();
    HAL_NVIC_SetPriority(LPTIM_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(LPTIM_IRQn);
  } else __BKPT();
}

void HAL_LPTIM_MspDeInit(LPTIM_HandleTypeDef* lptimHandle)
{
  if(lptimHandle->Instance==MY_INSTANCE)
  {
    CLK_DIS();
    HAL_NVIC_DisableIRQ(LPTIM_IRQn);
  } else __BKPT();
}
