#include "main.h"
#include "battery.h"
#include "adc.h"
#include "serial.h"
#include "frame_ops.h"

#define V_ADC (float)2.5
#define ADC_BITS_FS (float)4095
#define V_TO_MV (float)1000

#define ADC_COUNT_BIAS 0
#define ADC_COUNT_FLOOR 0 // if, after bias subtract, this value will floor to zero.

#define ADC_CHANS 9

//#define battery_printf printf_frame
#ifndef battery_printf
#define battery_printf debug_printf
#endif

static uint16_t adc_data[ADC_CHANS] __attribute__ ((aligned(4)));
static ADC_HandleTypeDef *adc = &hadc1;
static battery_t battery;

battery_t * get_battery(void) { return &battery; }

// Returns uA
static uint32_t get_batt_ua(uint16_t x) {
	const float Rl = 100000; // External resistor ohms
	const float R_int = 5000; // Fixed by chip ohms
	const float Rs = 0.15; // Sense resistor value ohms
	float ret;
	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = ret * R_int / Rl / Rs; // mA
	ret = ret * V_TO_MV; // uA
	return (uint32_t) ret;
}

static uint32_t get_batt_mvolt(uint16_t x) {
	/* Divider resistors */
	const float R3 = 150;
	const float R4 = 51; // measured across R4
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = (ret*(R3+R4))/R4; // Factor in voltage divider
	return (uint32_t)ret;
}

static uint32_t get_solar_mvolt(uint16_t x) {
	/* Divider resistors */
	const float R3 = 2320;
	const float R4 = 232;
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = (ret*(R3+R4))/R4; // Factor in voltage divider
	return (uint32_t)ret;
}

// Cell 3 is special, hack
#ifdef I_AM_REV01_BRD
static uint32_t get_batt_mvolt_cell3(uint16_t x) {
	/* Divider resistors */
	const float R3 = 1500;
	const float R4 = 232; // Adjusted BOM value in Rev01
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = (ret*(R3+R4))/R4; // Factor in voltage divider
	return (uint32_t)ret;
}
#else
static uint32_t get_batt_mvolt_cell3(uint16_t x) {
	/* Divider resistors */
	const float R3 = 1500;
	// NOT ALL UNITS HAVE BELOW MOD. --NPS 2019-11-26
	const float R4 = 291.42857; // 510 || 680
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = (ret*(R3+R4))/R4; // Factor in voltage divider
	return (uint32_t)ret;
}
#endif

// Vout = Tc x Ta + V_oc
// Ta = (Vout - V_oc) / Tc
// Returns: Degrees C ambient
static float get_temperature(uint16_t x) __attribute__ ((unused));
static float get_temperature(uint16_t x) {
	const float c = 0.5;   // 500mV at 0 deg C
	const float Tc = 0.01; // 10 mV per deg C
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC; // V
	return (ret - c)/Tc;
}

static float c_to_f(float c) __attribute__ ((unused));
static float c_to_f(float c) {
	return 1.8*c+32;
}

// Total Stack
static uint32_t get_batt_stack_mvolt(uint16_t x) {
	/* Divider resistors */
	const float R3 = 1500;
	const float R4 = 232; // measured across R4
	float ret;

	ret = (float)x / ADC_BITS_FS * V_ADC * V_TO_MV; // mV
	ret = (ret*(R3+R4))/R4; // Factor in voltage divider
	return (uint32_t)ret;
}

static uint16_t remove_adc_bias(uint16_t x) {
	if (x >= ADC_COUNT_BIAS)
		x = x - ADC_COUNT_BIAS;
	if (x <= ADC_COUNT_FLOOR)
		return 0;
	else return x;
}

static uint32_t solar_get_average(const uint32_t *data, const int len) {
	uint32_t ret=0;
	for(int i=0; i<len; i+=2) {
#ifdef _DEBUG
		uint32_t ret_debug = ret;
#endif
		ret += data[i];
		ret += data[i+1];
#ifdef _DEBUG
		if (ret_debug > ret) MY_BKPT(); // should not overflow but sanity check
#endif
	}
	return ret / len; // Average
}

// Input is 10mV x 10uA on nominal 1800 data points (1 hours worth)
// Target is mWh
// Doing this the naive way will overflow
// This compromise will should retain some precision
static uint32_t hour_data_to_mWh(const uint32_t *data, const int len) {
	uint32_t ret=0;
	for(int i=0; i<len; i+=4) {
		uint32_t ret_inner=0;
#ifdef _DEBUG
		uint32_t ret_debug = ret;
#endif
		ret_inner += data[i+0];
		ret_inner += data[i+1];
		ret_inner += data[i+2];
		ret_inner += data[i+3];
		ret_inner /= 100;
		ret += ret_inner;
#ifdef _DEBUG
		if (ret_debug > ret) MY_BKPT(); // should not overflow but sanity check
#endif
	}
	return (ret / 100) / len;
}

#define BATT_STRING_MAX_CHARS 8
// TODO: This is messy NPS
void report_battery(uint32_t tick) {
	static uint32_t solar_avg;
	static uint32_t batts_mv_avg[5];
	static uint32_t current_ua_avg[2];
	static uint16_t temp_avg;
	uint32_t batts_mv[5];
	float degC;
	uint32_t current_ua[2];
	//uint16_t adc_vref = adc_data[ADC_CHANS-1]; // Not using adc_vref, known LDO regulation seems to be better
	char batt_string[4][BATT_STRING_MAX_CHARS];

	//for(int i=0; i<4; i++) { CELL 3 HAS BAD ADC INPUT, SEE ERRATA 15
	for(int i=0; i<3; i++) {
		uint16_t data = adc_data[i];
		batts_mv[i] = get_batt_mvolt(data);
	}

	batts_mv[3] = get_batt_mvolt_cell3(adc_data[3]); // hack
	batts_mv[4] = get_batt_stack_mvolt(adc_data[4]);

	// Knock a count or two off for the current measurements so we can actually hit zero
	for(int i=6; i<=7; i++) {
		adc_data[i] = remove_adc_bias(adc_data[i]);
	}

	current_ua[0] = get_batt_ua(adc_data[6]); // Battery OUT
	current_ua[1] = get_batt_ua(adc_data[7]); // Battery IN

	for(int i=3; i>0; i--) { // Remove bias
		batts_mv[i] = batts_mv[i] - batts_mv[i-1];
	}

	// Update window totals
	batts_mv_avg[0] += batts_mv[0];
	batts_mv_avg[1] += batts_mv[1];
	batts_mv_avg[2] += batts_mv[2];
	batts_mv_avg[3] += batts_mv[3];
	batts_mv_avg[4] += batts_mv[4];
	current_ua_avg[0] += current_ua[0];
	current_ua_avg[1] += current_ua[1];
	temp_avg += adc_data[5];
	solar_avg += get_solar_mvolt(adc_data[8]);

	// Only report on battery status every BATT_REPORT_INTERVAL RTC ticks
	if (tick % BATT_REPORT_INTERVAL != BATT_REPORT_INTERVAL-1) return;

	batts_mv[0] = batts_mv_avg[0] / BATT_REPORT_INTERVAL;
	batts_mv[1] = batts_mv_avg[1] / BATT_REPORT_INTERVAL;
	batts_mv[2] = batts_mv_avg[2] / BATT_REPORT_INTERVAL;
	batts_mv[3] = batts_mv_avg[3] / BATT_REPORT_INTERVAL;
	batts_mv[4] = batts_mv_avg[4] / BATT_REPORT_INTERVAL;
	current_ua[0] = current_ua_avg[0] / BATT_REPORT_INTERVAL;
	current_ua[1] = current_ua_avg[1] / BATT_REPORT_INTERVAL;
	temp_avg = temp_avg / BATT_REPORT_INTERVAL;

	battery.solar_input_mv = solar_avg / BATT_REPORT_INTERVAL;

	// Reset
	memset(batts_mv_avg, 0, sizeof(batts_mv_avg));
	memset(current_ua_avg, 0, sizeof(current_ua_avg));
	solar_avg = 0;

	degC = get_temperature(temp_avg);
	//battery_printf("Temperature: %.1fC %.1fF (%u)\r\n", degC, c_to_f(degC), temp_avg);
	temp_avg = 0;

	for(int i=0; i<4; i++) { // Show bleed status
		char c;
		if (battery.bleed[i] == BLEED_ON) c = '*';
		else c = '\0';
		snprintf(batt_string[i], BATT_STRING_MAX_CHARS, "%lu%c", batts_mv[i], c);
	}

	battery_printf("BMS %lu: %s %s %s %s %lu %lu %lu %lu %.1fC\r\n",
		tick/BATT_REPORT_INTERVAL, batt_string[0], batt_string[1], batt_string[2], batt_string[3], batts_mv[4],
		battery.solar_input_mv, current_ua[0], current_ua[1], degC);

	memcpy(battery.cells, batts_mv, sizeof(battery.cells));
	battery.tot = batts_mv[4];

	// Update history data. Divide by 10 to cull the precision a little and avoid overflow
	battery.power_out_data[battery.data_idx]   = (current_ua[0]/10)*(battery.tot/10);
	battery.power_in_data[battery.data_idx]    = (current_ua[1]/10)*(battery.tot/10);
	battery.solar_volt_data[battery.data_idx]  = battery.solar_input_mv;
	battery.power_in_recent = (current_ua[1]/10)*(battery.tot/10) / 10000; // mW
	battery.temperature = degC;
	battery.data_idx++;
	if (battery.data_idx == REPORTS_PER_HOUR) { // nom 1800, takes 5ms compute
		battery.data_idx = 0;
		battery.power_in_24[battery.hour_idx]   = hour_data_to_mWh(battery.power_in_data,    REPORTS_PER_HOUR);
		battery.power_out_24[battery.hour_idx]  = hour_data_to_mWh(battery.power_out_data,   REPORTS_PER_HOUR);
		battery.solar_volt_24[battery.hour_idx] = solar_get_average(battery.solar_volt_data, REPORTS_PER_HOUR);
		battery.temperature_24[battery.hour_idx]= degC; // Don't average etc, just record it every hour
		battery.hour_idx++;
		if (battery.hour_idx == HOURS_PER_DAY) battery.hour_idx = 0;
	}

	// Back to main.c which may take action on new status
	battery_status_update(&battery);
}

int battery_mgmt(unsigned tick) {
	battery_clock_lock(BATT_LOCK_ON);
	HAL_ADC_Start_DMA(adc, (uint32_t *)adc_data, ADC_CHANS);
	return 0;
}