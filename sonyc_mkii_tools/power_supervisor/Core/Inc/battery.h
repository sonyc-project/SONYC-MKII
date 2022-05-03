
#include "main.h"
#include "time_ticks.h"

#define BATT_LOCK_ON  1
#define BATT_LOCK_OFF 0

typedef enum { BLEED_OFF=0, BLEED_ON } batt_bleed_t;

typedef struct {
	uint32_t ver;								// struct version. Not real, here for consistency...
	uint32_t cells[4];							// Cell voltage in mV
	uint32_t solar_input_mv;					// Solar input voltage in mV
	uint32_t tot;								// Total battery stack voltage in mV
	uint32_t data_idx;
	uint32_t hour_idx;
	uint32_t power_in_recent;					// Temp copy of the most recent power input in mW
	uint32_t power_in_data[REPORTS_PER_HOUR];	// Full data from past hour
	uint32_t power_out_data[REPORTS_PER_HOUR];
	uint32_t solar_volt_data[REPORTS_PER_HOUR];
	uint32_t power_in_24[HOURS_PER_DAY];		// Rolling 24 hour window
	uint32_t power_out_24[HOURS_PER_DAY];
	uint32_t solar_volt_24[HOURS_PER_DAY];
	float temperature;							// in degC
	float temperature_24[HOURS_PER_DAY];
	batt_bleed_t bleed[4];						// Bleed (balancer) status, ON or OFF
} battery_t;

battery_t * get_battery(void);

void battery_clock_lock(unsigned x);
void battery_status_update(battery_t *x);
void report_battery(uint32_t tick);
int battery_mgmt(unsigned tick);