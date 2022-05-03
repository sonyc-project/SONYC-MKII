/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2020 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

#include <stdbool.h>

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"
#include "serial.h"

#define printf debug_printf

#define TURN_ON 1
#define TURN_OFF 0

// All in VOLTS (float)
#define DISCHARGE_LIMIT 	1.550
#define FAST_CHARGE_LIMIT 	2.100
#define CHARGE_DONE 		2.740
#define CHARGE_DONE_SEC		3		// Cell voltage must stick for X seconds

void SystemClock_Config(void);
static void do_charge_button(void);

// Assumes sane inputs
static float find_max_float(float *x, int len) {
	float max=x[0];
	for(int i=1; i<len; i++) {
		if (x[i] > max)
			max = x[i];
	}
	return max;
}

static void set_led_green(int x) {
	if (x > 0)
		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_RESET);
	else
		HAL_GPIO_WritePin(LED_GREEN_GPIO_Port, LED_GREEN_Pin, GPIO_PIN_SET);
}

// x > 0 to enable LED
static void set_led_red(int x) {
	if (x > 0)
		HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_RESET);
	else
		HAL_GPIO_WritePin(LED_RED_GPIO_Port, LED_RED_Pin, GPIO_PIN_SET);
}

// Enables or disables the 2.8v chargers
static void set_en_2v8(int x) {
	if (x > 0)
		HAL_GPIO_WritePin(EN_2V8_GPIO_Port, EN_2V8_Pin, GPIO_PIN_SET);
	else
		HAL_GPIO_WritePin(EN_2V8_GPIO_Port, EN_2V8_Pin, GPIO_PIN_RESET);
}

static void set_mux_en(int x) {
	if (x > 0)
		HAL_GPIO_WritePin(MUX_EN_GPIO_Port, MUX_EN_Pin, GPIO_PIN_RESET);
	else
		HAL_GPIO_WritePin(MUX_EN_GPIO_Port, MUX_EN_Pin, GPIO_PIN_SET);
}

static void set_charge(int channel, int x) {
	GPIO_TypeDef *port;
	uint16_t pin;
	GPIO_PinState newstate;

	newstate = (x>0) ? GPIO_PIN_RESET : GPIO_PIN_SET;
	switch(channel) {
		case 0: port = CHG_A0_GPIO_Port; pin = CHG_A0_Pin; break;
		case 1: port = CHG_A1_GPIO_Port; pin = CHG_A1_Pin; break;
		case 2: port = CHG_A2_GPIO_Port; pin = CHG_A2_Pin; break;
		case 3: port = CHG_A3_GPIO_Port; pin = CHG_A3_Pin; break;
		case 4: port = CHG_B0_GPIO_Port; pin = CHG_B0_Pin; break;
		case 5: port = CHG_B1_GPIO_Port; pin = CHG_B1_Pin; break;
		case 6: port = CHG_B2_GPIO_Port; pin = CHG_B2_Pin; break;
		case 7: port = CHG_B3_GPIO_Port; pin = CHG_B3_Pin; break;
		default:  __BKPT(); return; // invalid channel
	}
	HAL_GPIO_WritePin(port, pin, newstate);
}

static void clear_all_fast_charge(void) {
	for(int i=0; i<8; i++)
		set_charge(i, TURN_OFF);
}

static void set_discharge(int channel, int x) {
	GPIO_TypeDef *port;
	uint16_t pin;
	GPIO_PinState newstate;

	newstate = (x>0) ? GPIO_PIN_SET : GPIO_PIN_RESET;
	switch(channel) {
		case 0: port = DCHG_A0_GPIO_Port; pin = DCHG_A0_Pin; break;
		case 1: port = DCHG_A1_GPIO_Port; pin = DCHG_A1_Pin; break;
		case 2: port = DCHG_A2_GPIO_Port; pin = DCHG_A2_Pin; break;
		case 3: port = DCHG_A3_GPIO_Port; pin = DCHG_A3_Pin; break;
		case 4: port = DCHG_B0_GPIO_Port; pin = DCHG_B0_Pin; break;
		case 5: port = DCHG_B1_GPIO_Port; pin = DCHG_B1_Pin; break;
		case 6: port = DCHG_B2_GPIO_Port; pin = DCHG_B2_Pin; break;
		case 7: port = DCHG_B3_GPIO_Port; pin = DCHG_B3_Pin; break;
		default:  __BKPT(); return; // invalid channel
	}
	HAL_GPIO_WritePin(port, pin, newstate);
}

static void clear_all_discharge(void) {
	for(int i=0; i<8; i++)
		set_discharge(i, TURN_OFF);
}

// MUX0 (PB5) is LSB
// PB7 PB6 PB5
#define MUX_CH_MASK (0x7 << 5)
static void set_mux_ch(int x) {
	uint32_t val;
	if (x<0 || x>7) { __BKPT(); return; } // only 0-7 valid

	__disable_irq();
	val = GPIOB->ODR;
	val &= ~MUX_CH_MASK;
	val |= x << 5;
	GPIOB->ODR = val;
	__enable_irq();
}

static float convert_to_volts(uint32_t raw) {
	return raw/4095.0*3.0;
}

// enums
typedef enum {
	state_none=0,
	state_charge=1,
	state_discharge=2,
	state_charge_wait=4,
} global_state_t;

// GENERAL DATA VARS
static uint32_t batt_raw[8];
static float batt[8];
static global_state_t global_state;
// END DATA VARS

// IRQ HANDLERS

static volatile bool chg_btn;
static volatile bool dchg_btn;
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	if (GPIO_Pin == GPIO_PIN_0)	chg_btn = true;
	if (GPIO_Pin == GPIO_PIN_10)	dchg_btn = true;
}

static volatile uint32_t adc_value;
static volatile bool adc_ready;
void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *adc) {
	adc_value = HAL_ADC_GetValue(adc);
	adc_ready = true;
}

#define TICKS_PER_SECOND			50
#define PROCESS_TICK_START 			40
#define BATT_SAMPLES_PER_SEC		5
#define DISCHARGE_MODULO			10 		// e.g. ticks % 10 == 10% discharge mosfet PWM. Should be divisor of TICKS_PER_SECOND
#define EXTRA_DISCHARGE_VOLT_LIMIT	2.200 	// If all batteries are under this we can safely work the mosfets harder
#define EXTRA_DISCHARGE_MODULO		5		// The new duty cycle in above scenario e.g. 2x as much

static volatile uint32_t time_s;
static volatile uint32_t time_ticks; // 20ms nominal aka 50 ticks per second. First 40 ticks for sampling then 10 ticks for processing
static volatile bool new_tick;
static volatile bool new_second;
static uint32_t charge_done_ts;
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim) {
	__disable_irq();
	if (time_ticks == TICKS_PER_SECOND-1) {
		time_ticks = 0;
		time_s++;
		new_second = true;
	} else {
		time_ticks++;
	}
	new_tick = true;
	__enable_irq();
}

// END IRQ HANDLERS


// Due to alignment issues, on tick PROCESS_TICK_START we sample but don't change the mux
// and on tick 0 we don't sample but do change the mux
static void sample_battery(uint32_t tick) {
	uint32_t next_channel = tick % 8; // the NEXT channel
	uint32_t this_channel = (next_channel+7) % 8;

	if (tick > 0) {
		HAL_ADC_Start_IT(&hadc1);
		while(adc_ready == false) __WFI();
		HAL_ADC_Stop_IT(&hadc1);
		adc_ready = false;
		batt_raw[this_channel] += adc_value;
	}

	if (tick == PROCESS_TICK_START) return; // do not change the mux setting

	// Set next channel
	set_mux_ch(next_channel);
}

// Channel order 0 1 3 2 physically, have to compensate
// FOR DISPLAY ONLY
static int workaround_get_next(int prev) {
	switch (prev) {
		case 0: return 1;
		case 1: return 3;
		case 3: return 2;
		case 2: return 4;
		default: return prev+1;
	}
}

static void process_data() {
	if (time_s == 0) return; // Skip 0th for init etc
	printf("T %lu ", time_s);
	for(int i=0,j=0; i<8; j++, i = workaround_get_next(i)) {
		batt[i] = convert_to_volts(batt_raw[i]/BATT_SAMPLES_PER_SEC);
		if (i<7)
			printf("%d %.3f ", j, batt[i]);
		else
			printf("%d %.3f S %d\r\n", j, batt[i], global_state);
	}

	if (global_state & state_charge_wait) {
		if (time_s - charge_done_ts < CHARGE_DONE_SEC) {
			for(int x=0; x<8; x++) {
				if (batt[x] < CHARGE_DONE) {
					do_charge_button();
					__disable_irq();
					global_state &= ~state_charge_wait;
					__enable_irq();
					break;
				}
			}
		} else {
			__disable_irq();
			global_state &= ~state_charge_wait;
			__enable_irq();
		}
	}

	if (global_state & state_charge) {
		int charge_done=0;
		for(int x=0; x<8; x++) {
			if (batt[x] >= FAST_CHARGE_LIMIT) set_charge(x, TURN_ON);
			else set_charge(x, TURN_OFF);

			if (batt[x] >= CHARGE_DONE)
				charge_done++;
		}

		if (charge_done == 8) {
			__disable_irq();
			global_state &= ~state_charge;
			global_state |= state_charge_wait;
			charge_done_ts = time_s;
			__enable_irq();
			set_en_2v8(TURN_OFF);
			clear_all_fast_charge();
		}

	}
}

static void do_discharge_pwm(int turn_on) {
	int tot=0;
	static int done_second = -1;
	// The simple off case
	if(turn_on == 0) {
		clear_all_discharge();
		return;
	}

	// Flip on discharge mosfets for batts over the limit
	for(int i=0; i<8; i++) {
		if (batt[i] > DISCHARGE_LIMIT) {
			tot++;
			set_discharge(i, TURN_ON);
		} else {
			set_discharge(i, TURN_OFF);
		}
	}

	// If no batteries are over the limit we are done
	// Don't declare victory until under limit for 3 seconds
	if (tot == 0) {
		if (done_second < 0) {
			done_second = time_s;
			return;
		} else if (time_s - done_second < 3) {
			return;
		}
		__disable_irq();
		global_state &= ~state_discharge;
		__enable_irq();
		clear_all_discharge(); // should be redundant but lets be sure
		done_second = -1;
	} else {
		done_second = -1;
	}
}

static void do_charge_button(void) {
	__disable_irq();
	global_state &= ~state_discharge;
	global_state |= state_charge;
	time_s = 0; // reset time for easy book keeping
	__enable_irq();

	set_en_2v8(TURN_ON);
}

static void do_discharge_button(void) {
	__disable_irq();
	global_state &= ~state_charge;
	global_state |= state_discharge;
	time_s = 0; // reset time for easy book keeping
	__enable_irq();

	set_en_2v8(TURN_OFF);
	clear_all_fast_charge();
}

#define CLEAR_ARR(x) memset(x, 0, sizeof(x))
static void do_each_second(uint32_t seconds) {
	CLEAR_ARR(batt_raw);

	if (global_state & state_discharge) set_led_red(seconds & 1);
	else set_led_red(TURN_OFF);

	if (global_state & state_charge) set_led_green(seconds & 1);
	else set_led_green(TURN_OFF);
}

// No 0 tick on startup (ie at 0 ticks 0 seconds)
// But there is a 0 second tick (presently ignored)
static void do_each_tick(uint32_t tick) {
	float max_batt;
	static uint32_t my_modulo = DISCHARGE_MODULO;
	if (tick < PROCESS_TICK_START) {
		sample_battery(tick);
	} else if (tick == PROCESS_TICK_START) { // Allows 200ms to process and print (10 ticks)
		sample_battery(tick);
		process_data();

		// Compute some stuff for discharge
		max_batt = find_max_float(batt, 8);
		if (max_batt < EXTRA_DISCHARGE_VOLT_LIMIT)
			my_modulo = EXTRA_DISCHARGE_MODULO;
		else
			my_modulo = DISCHARGE_MODULO;
		// End discharge stuf

		if (chg_btn) {
			do_charge_button();
			chg_btn = false;
		}
		if (dchg_btn) {
			my_modulo = DISCHARGE_MODULO;
			do_discharge_button();
			dchg_btn = false;
		}
	}

	// Discharge PWM
	if (global_state & state_discharge) {
		if (tick % my_modulo == 0)
			do_discharge_pwm(TURN_ON);
		else
			do_discharge_pwm(TURN_OFF);
	}
}

int main(void)
{
	HAL_Init();

	SystemClock_Config();

	MX_GPIO_Init();
	MX_ADC1_Init();
	MX_TIM1_Init();
	MX_USART2_UART_Init();

	set_en_2v8(TURN_OFF);
	set_mux_ch(0); __NOP();
	set_mux_en(TURN_ON);

	do_each_second(0);
	//do_each_tick(0); // Don't do this
	HAL_TIM_Base_Start_IT(&htim1);
	HAL_DBGMCU_EnableDBGSleepMode();
	HAL_SuspendTick();

	while (1) {
		while (new_tick == false) __WFI(); // Wait for new tick
		new_tick = false;
		do_each_tick(time_ticks);
		if (new_second) {
			new_second = false;
			do_each_second(time_s);
		}
	}
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_NONE;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
  /** Initializes the CPU, AHB and APB buses clocks
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV8;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
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
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
