#pragma once

#define RTC_MS_PER_TICK 500 // Increasing this rate because I think I screwed up the ADC anti-aliasing filter
#define TICKS_PER_MINUTE (1000/RTC_MS_PER_TICK*60)
#define TICKS_PER_HOUR (1000/RTC_MS_PER_TICK*3600)
#define TICKS_PER_DAY (TICKS_PER_HOUR*24)

// Number of RTC ticks over which to average and report battery status
// To avoid possibility of overflow, keep strictly <= 16
#define BATT_REPORT_INTERVAL 4 // At 500ms per tick, report every 2 seconds

#define REPORTS_PER_HOUR (TICKS_PER_HOUR/BATT_REPORT_INTERVAL)
#define REPORTS_PER_DAY (REPORTS_PER_HOUR*24)

#define HOURS_PER_DAY 24
