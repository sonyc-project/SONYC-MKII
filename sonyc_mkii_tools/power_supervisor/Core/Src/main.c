#include <stdlib.h>
#include <stdbool.h>
#include "main.h"
#include "adc.h"
#include "crc.h"
#include "dma.h"
#include "rtc.h"
#include "spi.h"
#include "usart.h"
#include "gpio.h"
#include "tim.h"
#include "iwdg.h"

#include "serial.h"
#include "h7_bootloader.h"
#include "battery.h"
#include "serial_frame.h"
#include "enums.h"
#include "bms_serial.h"
#include "frame_ops.h"

//#define NO_DEBUG_UART2 // Disables the debug uart header
#define SOLAR_MPPC_START_MV 12000
//#define SOLAR_MPPC_STOP_MV  10000	// Not used in favor of below
#define SOLAR_MPPC_RETREAT_TO_HICCUP_POWER 200 // Under this value (100mW) prefer hiccup mode
#define TEMPERATURE_CUTOFF	-25 // Degrees C
#define TEMPERATURE_TURNON	-10
//#define LAB_BENCH_MODE // Assumes wired directly to bench supply and no batteries. Suppresses warnings and allows normal ops.

#define BMS_DATA_FORMAT_VER 7 // also see below

#define HELLO_STRING "SONYC Mel BMS Compiled " __DATE__ " " __TIME__ "\r\n"
#define VERSION_STRING "BMS firmware rev 8\r\n"

//#define KILL_H7

#define H7_ON  0
#define H7_OFF 1

#define RAIL_ON  1
#define RAIL_OFF 0

#define LOCK_ON  1
#define LOCK_OFF 0

#define TURN_ON 1
#define TURN_OFF 0

#define TIMEOUT_MAX (2*1000) // 2000ms
#define JTAG_STARTUP_PAUSE_MS 4000

//#define TIM6_PRESCALE 400
#define TIM6_PRESCALE 320 // Should be more optimal
#define HSI_NOM_KHZ 8000
#define CALIB_TIM (&htim6)

typedef enum {
	CLOCK_LOCK_NONE		= 0x0,
	CLOCK_LOCK_BATTERY  = 0x1,
	CLOCK_LOCK_CALIB	= 0x2,
	CLOCK_LOCK_BUTTON	= 0x4,
	CLOCK_LOCK_UART_H7	= 0x8,
} clock_lock_t;

#define BASE_TO_PORT(x) x##_GPIO_Port
#define BASE_TO_PIN(x)  x##_Pin
#define MY_HAL_WRITE_PIN(x,y) HAL_GPIO_WritePin(BASE_TO_PORT(x), BASE_TO_PIN(x), y)

#define RTC_CALIB_FACTOR (RTC_MS_PER_TICK/1000)

#define BLEED_AND_CHARGE_MV 2300

#ifdef NO_DEBUG_UART2
#define debug_printf(...) ((void)0)
#endif

//#define debug_printf printf_frame // for printing to the H7 uart

// Moved to main.h
//#define I_AM_REV01_BRD // Build 6409, circa Nov 2019, has a few hardware differences

// Globals
static bool is_fast_mode = true; // defaults to 8 MHz (fast mode)
static clock_lock_t clock_lock; // keeps the clock at 8 MHz when active
static volatile uint32_t tick;  // 0.5 second RTC tick counter, rollover at 68 years, ignoring
static volatile bool rts_was_signaled;
static volatile bool button_pressed;
static volatile bool adc_dma_ready;
static bool use_rtc_tick;

// Static func prototypes
static void slow_clock_config(void);
static void fast_clock_config(void);
static void set_clock_lock(clock_lock_t x);
static void unset_clock_lock(clock_lock_t x);

// Interrupt handlers
static void do_RTS_signal(void);
static void do_PA0_button(void);

// System
void SystemClock_Config(void);

static void print_compile_info(void) {
	debug_printf("Compiled: %s %s\r\n", __DATE__, __TIME__);
}

static void print_serial(void) {
	uint32_t UID[3];
	//HAL_GetUID(UID); // Removed in HAL 1.8
	UID[0] = HAL_GetUIDw0();
	UID[1] = HAL_GetUIDw1();
	UID[2] = HAL_GetUIDw2();
	debug_printf("Serial Number 0x%lx%lx%lx\r\n", UID[0], UID[1], UID[2]);
}

// Used for lowest power when external pull-down is present
static void set_pin_analog(GPIO_TypeDef *port, uint16_t pin) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_RESET); __NOP(); __NOP(); __NOP(); __NOP(); // drain it a little to speed things up
	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void set_pin_high_pp(GPIO_TypeDef *port, uint16_t pin) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	HAL_GPIO_WritePin(port, pin, GPIO_PIN_SET);
	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
	GPIO_InitStruct.Pull = GPIO_NOPULL;
	GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void set_pin_input_pullup(GPIO_TypeDef *port, uint16_t pin) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLUP;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void set_pin_input_pulldown(GPIO_TypeDef *port, uint16_t pin) {
	GPIO_InitTypeDef GPIO_InitStruct = {0};
	GPIO_InitStruct.Pin = pin;
	GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
	GPIO_InitStruct.Pull = GPIO_PULLDOWN;
	HAL_GPIO_Init(port, &GPIO_InitStruct);
}

static void set_h7_boot0(h7_boot0_state_t x) __attribute__ ((unused));
static void set_h7_boot0(h7_boot0_state_t x) {
	GPIO_TypeDef *PORT = 	H7_BOOT_0_GPIO_Port;  // make sure I match
	uint16_t PIN = 			H7_BOOT_0_Pin;		  // make sure I match

	if (x == H7_BOOT0_SET)
		set_pin_high_pp(PORT, PIN);
	if (x == H7_BOOT0_RESET) {
		set_pin_analog(PORT, PIN); // external pull-down
	}
}

static bool is_1v8_on = false;
static bool get_1v8(void) {
	return is_1v8_on;
}

// External pull-down
static void set_1v8(int go) {
	GPIO_TypeDef *PORT = 	EN_1V8_GPIO_Port; // make sure I match
	uint16_t PIN = 			EN_1V8_Pin;		  // make sure I match

	if (go) {
		set_pin_high_pp(PORT, PIN);
		is_1v8_on = true;
	}
	else {
		set_pin_analog(PORT, PIN);
		is_1v8_on = false;
	}
}


// External pull-down
// Pull-down only present on ADP151, NOT ADP150 (which is otherwise pin-compatible).
static void set_1v8_rf(int go) {
	// FIXME: Designed for ADP151, which has internal pull-down, but testing one unit with ADP150, which does not
	if (go) {
		MY_HAL_WRITE_PIN(RF_EN_1V8, GPIO_PIN_SET);
		//set_pin_high_pp(PORT, PIN);
	}
	else {
		MY_HAL_WRITE_PIN(RF_EN_1V8, GPIO_PIN_RESET);
		//set_pin_analog(PORT, PIN);
	}

#ifdef I_AM_REV01_BRD
	if (go) {
		MY_HAL_WRITE_PIN(MCP_EN, GPIO_PIN_SET);
	}
	else {
		MY_HAL_WRITE_PIN(MCP_EN, GPIO_PIN_RESET);
	}
#endif
}

// This pin is pulled high/low, not output p-p.
// WARN: inverted logic (reset is asserted low)
// Default at startup is to keep chip in reset
static void set_h7_reset(int go) {
	GPIO_TypeDef *PORT = 	nH7_RESET_GPIO_Port; // make sure I match
	uint16_t PIN = 			nH7_RESET_Pin;		 // make sure I match
	if (go)
		set_pin_input_pulldown(PORT, PIN);
	else
		set_pin_input_pullup(PORT, PIN);
}

static int rtc_calibrate(void) {
	static uint16_t last_cnt=0;
	uint16_t cnt;
	uint32_t diff;

	TIM_TypeDef *tim = CALIB_TIM->Instance;
	cnt = tim->CNT;

	if (cnt > last_cnt) {
		diff = cnt-last_cnt;
	} else { // roll-over case
		diff = cnt+0x10000-last_cnt;
	}
	last_cnt = cnt;
	return diff;
}

static void setup_hsi_rtc_calibrate(void) {
	set_clock_lock(CLOCK_LOCK_CALIB);
	HAL_TIM_Base_Start(CALIB_TIM);
	rtc_calibrate();
	debug_printf("Calibrating HSI against 32768 Hz RTC...\r\n");
}

static void finish_hsi_rtc_calibrate(void) {
	uint32_t calib_ret = rtc_calibrate(); // 2nd call to get result after 1 tick
#if RTC_CALIB_FACTOR >= 1
	uint32_t hsi_est = calib_ret*TIM6_PRESCALE/RTC_CALIB_FACTOR;
#else
	uint32_t hsi_est = calib_ret*TIM6_PRESCALE*(1000/RTC_MS_PER_TICK);
#endif
	debug_printf("HSI calib: %lu kHz (offset: %ld)\r\n", hsi_est/1000, hsi_est/1000-HSI_NOM_KHZ);
	HAL_TIM_Base_MspDeInit(CALIB_TIM); // Done with this timer
	unset_clock_lock(CLOCK_LOCK_CALIB);
}

uint32_t get_rtc_tick(void) {
	return tick;
}

// Pair of functions to only unlock if they were initially unlocked
// In other words, nestable
static uint32_t lock_irq(void) {
	uint32_t ret = __get_PRIMASK(); // returns '1' if already locked
	if (ret == 0) __disable_irq();
	return ret;
}

static void unlock_irq(uint32_t locked) {
	if (locked) return; // If we were not the original locker, don't unlock
	else __enable_irq();
}

static void set_clock_lock(clock_lock_t x) {
	uint32_t irq = lock_irq();
	clock_lock |= x;
	unlock_irq(irq);
}

static void unset_clock_lock(clock_lock_t x) {
	uint32_t irq = lock_irq();
	clock_lock &= ~x;
	unlock_irq(irq);
}

static void sleep_perp_clocks(void) {
	HAL_UART_MspDeInit(&huart1);
#ifndef NO_DEBUG_UART2
	HAL_UART_MspDeInit(&huart2);
#endif
}

static void wake_perp_clocks(void) {
	HAL_UART_MspInit(&huart1);
#ifndef NO_DEBUG_UART2
	HAL_UART_MspInit(&huart2);
#endif
}

// Only used early in init
static void wait_one_rtc_tick(void) {
	uint32_t my_tick = tick;
	while (tick == my_tick) { __DMB(); __WFI(); }
}

static void set_mppc_mode(int x) {
	if (x) { // turn on
		set_pin_analog(MPPC_SET_GPIO_Port, MPPC_SET_Pin);
	} else {
		set_pin_high_pp(MPPC_SET_GPIO_Port, MPPC_SET_Pin);
	}
}

// Defaults to all charging off
static void solar_set_defaults(void) {

	// Disable USB mode
	HAL_GPIO_WritePin(PV_RUN_SET_1_GPIO_Port, PV_RUN_SET_1_Pin, GPIO_PIN_RESET);

	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_SET); // Default higher limit.

	// Sets all charging OFF
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_SET);

	// Some defaults which are mooted by above
	HAL_GPIO_WritePin(PV_RUN_SET_0_GPIO_Port, PV_RUN_SET_0_Pin, GPIO_PIN_SET);
	set_mppc_mode(TURN_OFF); // Hiccup mode
}

static void solar_set_hiccup(void) {
	HAL_GPIO_WritePin(PV_RUN_SET_0_GPIO_Port, PV_RUN_SET_0_Pin, GPIO_PIN_SET);		// Sets solar input
	set_mppc_mode(TURN_OFF);														// Hiccup mode
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_RESET);	// Enable run
}

static void solar_set_mppc(void) {
	HAL_GPIO_WritePin(PV_RUN_SET_0_GPIO_Port, PV_RUN_SET_0_Pin, GPIO_PIN_SET);		// Sets solar input
	set_mppc_mode(TURN_ON);															// MPPC Mode
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_RESET);	// Enable run
}

static void turn_off_all_charging(bool printme) {
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_SET);		// Assert all-off
	if (printme)
		debug_printf("Critical condition reached, all charging disabled.\r\n");
}

static void turn_off_usb_charging(void) {
	solar_set_defaults();
	debug_printf("Battery Charging from USB stopped.\r\n");
}

static void turn_on_usb_charging(void) {
	// Disable all charging while we config
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_SET);

	// Disable Solar Charing
	HAL_GPIO_WritePin(PV_RUN_SET_0_GPIO_Port, PV_RUN_SET_0_Pin, GPIO_PIN_RESET);

	// Enable USB charging
	HAL_GPIO_WritePin(PV_RUN_SET_1_GPIO_Port, PV_RUN_SET_1_Pin, GPIO_PIN_SET);

	// It will charge faster with neither pin set, but potentially out of spec of USB port
	// At my desk, this pulled 3 watts , for example --NPS
	//HAL_GPIO_WritePin(PV_RUN_SET_1_GPIO_Port, PV_RUN_SET_1_Pin, GPIO_PIN_RESET);

	// Set lower current limit
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_11, GPIO_PIN_RESET);

	// USB uses hiccup mode
	set_mppc_mode(TURN_OFF);

	// Remove RUN blocker
	HAL_GPIO_WritePin(PV_RUN_SET_2_GPIO_Port, PV_RUN_SET_2_Pin, GPIO_PIN_RESET);

	debug_printf("Battery Charging from USB activated\r\n");
}

static void bleed_cell_set(battery_t *batt) {
	if (batt->bleed[0] == BLEED_ON) HAL_GPIO_WritePin(EN_BAL_0_GPIO_Port, EN_BAL_0_Pin, GPIO_PIN_SET);
	else HAL_GPIO_WritePin(EN_BAL_0_GPIO_Port, EN_BAL_0_Pin, GPIO_PIN_RESET);

	if (batt->bleed[1] == BLEED_ON) HAL_GPIO_WritePin(EN_BAL_1_GPIO_Port, EN_BAL_1_Pin, GPIO_PIN_SET);
	else HAL_GPIO_WritePin(EN_BAL_1_GPIO_Port, EN_BAL_1_Pin, GPIO_PIN_RESET);

	if (batt->bleed[2] == BLEED_ON) HAL_GPIO_WritePin(EN_BAL_2_GPIO_Port, EN_BAL_2_Pin, GPIO_PIN_SET);
	else HAL_GPIO_WritePin(EN_BAL_2_GPIO_Port, EN_BAL_2_Pin, GPIO_PIN_RESET);

	if (batt->bleed[3] == BLEED_ON) HAL_GPIO_WritePin(EN_BAL_3_GPIO_Port, EN_BAL_3_Pin, GPIO_PIN_SET);
	else HAL_GPIO_WritePin(EN_BAL_3_GPIO_Port, EN_BAL_3_Pin, GPIO_PIN_RESET);
}

static void bleed_balance_update(battery_t *batt) {
	const int max_mV_diff = 80; // Battery cells are unbalanced if (max - min) >= max_mV_diff
	const int min_bleed_mV = BLEED_AND_CHARGE_MV; // Bleed cells with only >= this voltage
	int bleed_active = 0;
	uint32_t min=0xFFFFFFFF,max=0;

	// First pass
	for(int i=0; i<4; i++) {
		uint32_t x = batt->cells[i];
		if (batt->bleed[i] == BLEED_ON) bleed_active++;
		if (x < min) min = x;
		if (x > max) max = x;
	}

	// We are balanced, turn off all bleeds (if any) and done
	if (max < min_bleed_mV || (max-min) < max_mV_diff) {
		if (bleed_active == 0) return;
		for(int i=0; i<4; i++) batt->bleed[i] = BLEED_OFF;
		bleed_cell_set(batt);
		return;
	}

	// Some cells are considered unbalanced and in need of adjustment
	for(int i=0; i<4; i++) {
		uint32_t x = batt->cells[i];
		if (x > min_bleed_mV && (x - min) > max_mV_diff)
			batt->bleed[i] = BLEED_ON;
		else
			batt->bleed[i] = BLEED_OFF;
	}

	bleed_cell_set(batt);
}

#define IS_USB_POWER_PLUG	(HAL_GPIO_ReadPin(VBUS_DET_GPIO_Port, VBUS_DET_Pin) == GPIO_PIN_SET)
#define IS_USB_POWER_UNPLUG (HAL_GPIO_ReadPin(VBUS_DET_GPIO_Port, VBUS_DET_Pin) == GPIO_PIN_RESET)
// TODO: This is a mess. --NPS
void battery_status_update(battery_t *x) {
	const uint32_t max_batt_mv = 2750; // hysteresis
	const uint32_t min_batt_mv = BLEED_AND_CHARGE_MV;
	const uint32_t battery_cutoff_min = 1850; // Force H7 and RF off if ANY cell is <=
	const uint32_t battery_cutoff_max = 2100; // Allow back on if ALL cells are >=
	static int charge_state = 0; // 0 == OFF, 1 == USB, 2 == SOLAR_HICCUP, 3 == SOLAR_MPPC
	static int force_h7_off = 0;
	static bool display_crit = true;
	static uint32_t tick_last_full_charge = 0;
	int abort_charge = 0;
	int start_charge = 0;
	int power_cutoff_change = 0;

	bleed_balance_update(x);

	// If batteries are too low, cut off the 1.8v rail to the H7.
	if (force_h7_off == 0) {
		for(int i=0; i<4; i++) {
			if (x->cells[i] <= battery_cutoff_min) {
				force_h7_off = 1;
				power_cutoff_change = 1;
			}
		}
		if (x->temperature <= TEMPERATURE_CUTOFF) {
			force_h7_off = 1;
			power_cutoff_change = 1;
		}
	} else {
		if ( x->cells[0] >= battery_cutoff_max && x->cells[1] >= battery_cutoff_max && x->cells[2] >= battery_cutoff_max && x->cells[3] >= battery_cutoff_max && x->temperature >= TEMPERATURE_TURNON ) {
			force_h7_off = 0;
			power_cutoff_change = 1;
		}
	}

#ifndef LAB_BENCH_MODE
	if (power_cutoff_change) {
		if (force_h7_off) {
			set_1v8_rf(RAIL_OFF);
			set_1v8(RAIL_OFF);
			debug_printf("Battery or Temperature critical, disabling 1.8v\r\n");
		} else {
			set_1v8_rf(RAIL_ON);
			set_1v8(RAIL_ON);
			debug_printf("Battery and Temperature status normal, restoring 1.8v\r\n");
		}
	}
#endif

	// Cancel USB charging if unplugged
	if ( charge_state == 1 && IS_USB_POWER_UNPLUG ) {
		debug_printf("No USB detected, aborting charge\r\n");
		turn_off_usb_charging();
		charge_state = 0;
	}

	// Only start charging if all batteries are <= min
	// Stop charging if any battery is >= max
	for(int i=0; i<4; i++) {
		if (x->cells[i] >= max_batt_mv)
			abort_charge++;
		else if (x->cells[i] < min_batt_mv)
			start_charge++;
	}

	if (x->temperature <= TEMPERATURE_CUTOFF)
		abort_charge++;

	if (abort_charge > 0) {
		tick_last_full_charge = tick;
		turn_off_all_charging(display_crit);
		display_crit = false;
		charge_state = 0;
	}
	// Only start if all cells are at least partially depleted OR if its been a full day since we last topped up
	else if (start_charge == 4 || ((tick-tick_last_full_charge) > TICKS_PER_DAY) ) {
		if (IS_USB_POWER_PLUG) { // Prefer USB charging
			if (charge_state != 1) {
				turn_on_usb_charging();
				charge_state = 1;
			}
		}
		else {
			uint32_t solar_mv = x->solar_input_mv;
			uint32_t power_in_recent = x->power_in_recent;
			// If already in hiccup and enough power for MPPC
			if (solar_mv >= SOLAR_MPPC_START_MV && charge_state == 2 && power_in_recent > SOLAR_MPPC_RETREAT_TO_HICCUP_POWER) {
				debug_printf("Solar input at %lu mV, starting MPPC mode\r\n", solar_mv);
				solar_set_mppc();
				charge_state = 3;
			}
			// If in MPPC and not enough power, revert to hiccup
			//else if (solar_mv <= SOLAR_MPPC_STOP_MV && charge_state == 3) {
			else if (power_in_recent <= SOLAR_MPPC_RETREAT_TO_HICCUP_POWER && charge_state == 3) {
				debug_printf("Solar input at %lu mW, reverting to hiccup\r\n", power_in_recent);
				solar_set_hiccup();
				charge_state = 2;
			// when going to hiccup for the first time
			} else if (charge_state == 0) {
				solar_set_hiccup();
				charge_state = 2;
			}
		}
		display_crit = true;
	}
}

// IRQ START
void HAL_ADC_ErrorCallback(ADC_HandleTypeDef *hadc) {
	MY_BKPT();
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef *my_hadc1) {
	adc_dma_ready = true;
}

void HAL_RTCEx_RTCEventCallback(RTC_HandleTypeDef *hrtc) {
	tick++; // Main time keeper
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
	switch(GPIO_Pin) {
		case PA0_BUTTON_Pin: 	do_PA0_button(); return; break;
		case H7_UART_RTS_Pin: 	do_RTS_signal(); return; break;
		default: MY_BKPT(); return;
	}
}

// Provides a tick value in millisecond.
// After init, use the RTC version.
// This really shouldn't be used but is provided as fallback for ST drivers
extern __IO uint32_t uwTick; // from stm32f1xx_hal.c
uint32_t HAL_GetTick(void) {
	if (use_rtc_tick) {
		return tick * RTC_MS_PER_TICK;
	} else {
		return uwTick; // same as _weak in stm32f1xx_hal.c
	}
}

void reset_rts_state(void) {
	rts_was_signaled = false;
}

static void do_RTS_signal(void) {
	rts_was_signaled = true;
}

static void do_PA0_button(void) {
	button_pressed = true;
}

// IRQ END

// NON-STATIC START
void battery_clock_lock(unsigned x) {
	if (x)
		set_clock_lock(CLOCK_LOCK_BATTERY);
	else
		unset_clock_lock(CLOCK_LOCK_BATTERY);
}
// NON-STATIC END

// Called every tick
static void do_tick_update(unsigned my_tick) {
	battery_mgmt(my_tick);
}

#define battery_print_list(x) for(i=0; i<23; i++) debug_printf("%lu ", batt->x[i]); debug_printf("%lu\r\n", batt->x[23])

static void print_hour_stats(uint32_t hour) {
	int i;
	battery_t *batt = get_battery();
	debug_printf("HOUR %lu DATA\r\n", hour);
	debug_printf("POWER IN:\r\n"); battery_print_list(power_in_24);
	debug_printf("POWER OUT:\r\n"); battery_print_list(power_out_24);
	debug_printf("SOLAR V IN:\r\n"); battery_print_list(solar_volt_24);
}

// Dump collected data to framed binary format
// should be 24 x 4 x 4 + 4 + 16 + 4 + 4 = 412 bytes round up to 512
#define COPY_BATT(xxx) memcpy(&buf[len], batt->xxx, sizeof(batt->xxx)); len += sizeof(batt->xxx)
#define COPT_BATT_LIT(xxx) memcpy(&buf[len], &batt->xxx, sizeof(batt->xxx)); len += sizeof(batt->xxx)
//int serial_frame_encode_count(const uint8_t *in, uint32_t len_in, uint8_t dest, uint32_t pkt_type) {
static void send_hour_stats(void) {
	if (!get_1v8()) return; // Abort send if H7 isn't actually up
	static uint8_t buf[512]; // CHECK ME IF YOU TOUCH ANYTHING -- THIS MUST BE BIG ENOUGH
	uint8_t *encoded_buf = NULL;
	uint32_t encoded_buf_len;
	int len = 0;
	battery_t *batt = get_battery();

	// Copy out the binary data
	batt->ver = BMS_DATA_FORMAT_VER;
	COPT_BATT_LIT(ver);
	COPT_BATT_LIT(hour_idx);
	COPY_BATT(cells);
	COPT_BATT_LIT(tot);
	COPY_BATT(power_in_24);
	COPY_BATT(power_out_24);
	COPY_BATT(solar_volt_24);
	COPY_BATT(temperature_24);

	// Encode to serial frame
	encoded_buf_len = serial_frame_encode_count(buf, len, DEST_H7, FRAME_TYPE_BMS_STATS_v7);
	encoded_buf = (uint8_t *) malloc(encoded_buf_len);

	if (encoded_buf == NULL) {
		debug_printf("%s(): could not malloc() encode buffer, aborting\r\n", __func__);
		return;
	}

	len = serial_frame_encode(buf, len, encoded_buf_len, encoded_buf, DEST_H7, FRAME_TYPE_BMS_STATS_v7);
	if (len < 0 || len != encoded_buf_len) {  // sanity check
		debug_printf("%s(): serial_frame_encode() error returned %d\r\n", __func__, len);
		goto out;
	}

	debug_printf("Sending %d bytes to H7...\r\n", len);
	uint32_t now = HAL_GetTick();
	bms_transmit(encoded_buf, encoded_buf_len);
	debug_printf("Stats sent to H7 in %lu ms\r\n", HAL_GetTick()-now);
out:
	free(encoded_buf);
}

static void do_hour_update(uint32_t my_hour) {
	print_hour_stats(my_hour);
	send_hour_stats();

	// Re-calibrate the ADC every hour
	HAL_StatusTypeDef ret;
	ret = HAL_ADCEx_Calibration_Start(&hadc1); if (ret != HAL_OK) { MY_BKPT(); }
	ret = HAL_ADC_Stop(&hadc1); if (ret != HAL_OK) { MY_BKPT(); }
	debug_printf("ADC hourly re-calibration complete\r\n");
}

int main(void)
{
	SCB->VTOR = FLASH_BASE | 0x4000;
	__DSB(); __ISB(); // Flush pipeline after vector table change

	/* Reset of all peripherals, Initializes the Flash interface and the Systick. */
	HAL_Init();

	/* Configure the system clock */
	SystemClock_Config();

	/* Initialize all configured peripherals */
	MX_GPIO_Init();
#ifdef I_AM_REV01_BRD
	MX_GPIO_Init_Rev01();
#endif
	MX_DMA_Init();
	MX_ADC1_Init();
	MX_CRC_Init();
	MX_RTC_Init(RTC_MS_PER_TICK);
	MX_SPI1_Init();
	MX_USART1_UART_Init(); // UART to H7
#ifndef NO_DEBUG_UART2
	MX_USART2_UART_Init(); // UART to Debug
#endif
	MX_IWDG_Init(); // 26.2 seconds nominal watchdog
	MX_TIM6_Init(TIM6_PRESCALE);

	clock_lock = CLOCK_LOCK_NONE;

	//debug_printf("\033[2J"); // Clear screen
	//debug_printf("\033[H");  // Reset cursor
	//debug_printf("\033[37m");  // Black text https://www.student.cs.uwaterloo.ca/~cs452/terminal.html

	print_compile_info();
	print_serial();
	debug_printf(VERSION_STRING);
#ifdef I_AM_REV01_BRD
	debug_printf("Rev01 hardware build #6409 indicated. Using appropriate hardware config...\r\n");
#endif

	debug_printf("\r\nPausing for JTAG...\r\n");  // Black text https://www.student.cs.uwaterloo.ca/~cs452/terminal.html
	HAL_DBGMCU_EnableDBGSleepMode();
	HAL_Delay(JTAG_STARTUP_PAUSE_MS);
#ifndef _DEBUG
	HAL_DBGMCU_DisableDBGSleepMode();
#endif

#ifdef _DEBUG
	debug_printf("DEBUG BUILD FLAG\r\n");
#endif

	solar_set_defaults();

	debug_printf("Turning on 1.8v and 1.8v_RF\r\n");
	set_h7_boot0(H7_BOOT0_RESET);
	HAL_Delay(1);
	set_1v8(RAIL_ON);
	set_1v8_rf(RAIL_ON);

	{
	uint32_t timeout;
	timeout = HAL_GetTick();
	while (HAL_GPIO_ReadPin(PG_1V8_GPIO_Port, PG_1V8_Pin) == GPIO_PIN_RESET && HAL_GetTick() < timeout+TIMEOUT_MAX) { __WFI(); }
	if ( HAL_GPIO_ReadPin(PG_1V8_GPIO_Port, PG_1V8_Pin) == GPIO_PIN_RESET ) {
		set_1v8_rf(RAIL_OFF);
		set_1v8(RAIL_OFF);
		debug_printf("ERROR: 1.8v rail failure. Aborting...");
		MY_BKPT();
		goto out;
	}
	debug_printf("1.8v rail up in %lu ms\r\n", HAL_GetTick()-timeout);

#ifdef KILL_H7
	debug_printf("KILL_H7 set, disabling 1.8v rail\r\n");
	set_1v8_rf(RAIL_OFF);
	set_1v8(RAIL_OFF);
#else
	// bring the H7 out of reset
	debug_printf("Bringing H7 out of reset\r\n");
	set_h7_reset(H7_ON);
#endif

	HAL_StatusTypeDef ret;
	timeout = HAL_GetTick();
	debug_printf("Starting ADC calibration...\r\n");
	ret = HAL_ADCEx_Calibration_Start(&hadc1); if (ret != HAL_OK) { MY_BKPT(); }
	ret = HAL_ADC_Stop(&hadc1); if (ret != HAL_OK) { MY_BKPT(); }
	debug_printf("ADC Calibrated in %lu ms\r\n", HAL_GetTick()-timeout);
	}

	// I'm not convinced this matters, but ST datasheet says not to turn this on until H7 is ready
	// For now, hack it, and give it lots of time to come up.
	HAL_Delay(1000);
	HAL_GPIO_WritePin(GPIOC, VUSB_EN_FVT_Pin, GPIO_PIN_RESET);

out:
	// debug_printf("Starting LSE clock and turning off Systick\r\n");
	HAL_RTCEx_SetSecond_IT(&hrtc);

	// Calibration
	wait_one_rtc_tick();
	setup_hsi_rtc_calibrate();
	wait_one_rtc_tick();
	finish_hsi_rtc_calibrate();
	wait_one_rtc_tick();

	// Super Loop
	tick = 0;
	uint32_t last_tick = tick+1; // just to guarantee that we fire...
	while (1) {
		if (tick != last_tick) { // new tick
			do_tick_update(tick);
			last_tick = tick;

			// Do hourly tasks (101 offset is arb)
			if (last_tick % TICKS_PER_HOUR == TICKS_PER_MINUTE) do_hour_update(last_tick/TICKS_PER_HOUR);
		}

		uint32_t irq = lock_irq();
		__DMB(); __NOP(); // paranoia, probably useless
		if (!rts_was_signaled && !button_pressed && !adc_dma_ready && tick == last_tick) {
			slow_clock_config(); // NOP if the clock is locked
			__WFI();
		}
		fast_clock_config(); // Always go back to 8 MHz on wakeup.
		unlock_irq(irq);
		HAL_IWDG_Refresh(&hiwdg); // Kick the dog.

		// Pending interrupt (wakeup event) will get serviced here.
		// Should be: DMA, RTS (from H7), Button press, or RTC tick (common case)

		if (adc_dma_ready) {
			adc_dma_ready = false;
			report_battery(tick);
			battery_clock_lock(BATT_LOCK_OFF);
		}

		if (rts_was_signaled) {
			set_clock_lock(CLOCK_LOCK_UART_H7);
			bms_rx();
			unset_clock_lock(CLOCK_LOCK_UART_H7);
		}

		if (button_pressed) {
			set_clock_lock(CLOCK_LOCK_BUTTON);
			button_pressed = false;
			send_button_frame();
			unset_clock_lock(CLOCK_LOCK_BUTTON);
		}
	}
}

void * serial_frame_malloc(size_t size) { return malloc(size); }

// 8 / 8 = 1 MHz
// < 1 MHz messes up JTAG, but do it for production.
static void slow_clock_config(void) __attribute__ ((unused));
static void slow_clock_config(void) {
	uint32_t irq = lock_irq();
	if (clock_lock != CLOCK_LOCK_NONE) goto out; // clock locked, must stay in fast mode
	if (!is_fast_mode) goto out; // Already in slow mode
	sleep_perp_clocks();
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
							  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
#ifdef _DEBUG
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV8;
#else
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV16;
	//RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV64;
#endif
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { Error_Handler(); }
	HAL_SuspendTick(); // Turn off Systick interrupt which is enabled by above
	use_rtc_tick = true;
	is_fast_mode = false;
	__DMB(); __DSB();
out:
	unlock_irq(irq);
}

static void fast_clock_config(void) __attribute__ ((unused));
static void fast_clock_config(void) {
	uint32_t irq = lock_irq();
	if (is_fast_mode) goto out; // already in fast mode
	RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
	RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
							  |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
	RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_HSI;
	RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
	RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
	RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

	if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_0) != HAL_OK) { Error_Handler(); }
	wake_perp_clocks();
	uwTick = 0;
	use_rtc_tick = false;
	is_fast_mode = true;
	__DMB(); __DSB();
out:
	unlock_irq(irq);
}

// Default config on startup, should match fast clock config (8 MHz)
void SystemClock_Config(void)
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
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_RTC|RCC_PERIPHCLK_ADC;
  PeriphClkInit.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV4;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

void Error_Handler(void) { MY_BKPT(); }

#ifdef  USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line)
{
	MY_BKPT();
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     tex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
