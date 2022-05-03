#pragma once
#include "main.h"

LPTIM_HandleTypeDef hlptim1;

void MX_LPTIM_Init(void);
uint64_t lptim_get_us(void);
uint32_t lptim_get_ms(void);
