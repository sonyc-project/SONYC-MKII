/**
  ******************************************************************************
  * File Name          : ADC.c
  * Description        : This file provides code for the configuration
  *                      of the ADC instances.
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2019 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under BSD 3-Clause license,
  * the "License"; You may not use this file except in compliance with the
  * License. You may obtain a copy of the License at:
  *                        opensource.org/licenses/BSD-3-Clause
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "adc.h"

/* Overall, charge injection from ADC mux is much worse than anticipated --NPS */

/* Can keep low given the large c_ext */
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_1CYCLE_5
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_7CYCLES_5
#define BATTERY_SAMPTIME ADC_SAMPLETIME_13CYCLES_5
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_28CYCLES_5
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_55CYCLES_5
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_71CYCLES_5
//#define BATTERY_SAMPTIME ADC_SAMPLETIME_239CYCLES_5

/* Datasheet says 20 ohms, but this result implies much worse */
/* Seems stable at 41.5 @ ADC/4, going with 55.5 for margin */
//#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_13CYCLES_5
//#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_28CYCLES_5
//#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_41CYCLES_5
#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_55CYCLES_5
//#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_71CYCLES_5
//#define TEMPERATURE_SAMPTIME ADC_SAMPLETIME_239CYCLES_5

/*	This one is tricky, nano-power buffer is squeezed between
	driving the cap load and dealing with mux charge injection
	With no C_filt and R_iso 1.2kohm, by experiment, 30us sufficient
	Allowing some margin, going with 36us (ADC/4 x 71.5) */
//#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_13CYCLES_5
//#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_28CYCLES_5
//#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_41CYCLES_5
//#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_55CYCLES_5
#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_71CYCLES_5
//#define CURRENT_SENSOR_SAMPTIME ADC_SAMPLETIME_239CYCLES_5

ADC_HandleTypeDef hadc1;
DMA_HandleTypeDef hdma_adc1;

/* ADC1 init function */
void MX_ADC1_Init(void)
{
  ADC_ChannelConfTypeDef sConfig = {0};

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_ENABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 9;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }
  /** Configure Regular Channel
  */
	sConfig.Channel = ADC_CHANNEL_8; // BAL_0
	sConfig.Rank = ADC_REGULAR_RANK_1;
	sConfig.SamplingTime = BATTERY_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_15; // BAL_1
	sConfig.Rank = ADC_REGULAR_RANK_2;
	sConfig.SamplingTime = BATTERY_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_14; // BAL_2
	sConfig.Rank = ADC_REGULAR_RANK_3;
	sConfig.SamplingTime = BATTERY_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_9; // BAL_3
	sConfig.Rank = ADC_REGULAR_RANK_4;
	sConfig.SamplingTime = BATTERY_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_13; // Battery stack
	sConfig.Rank = ADC_REGULAR_RANK_5;
	sConfig.SamplingTime = BATTERY_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_1; // Temperature Sensor
	sConfig.Rank = ADC_REGULAR_RANK_6;
	sConfig.SamplingTime = TEMPERATURE_SAMPTIME; // Can be small because large C
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_11; // Battery OUT
	sConfig.Rank = ADC_REGULAR_RANK_7;
	sConfig.SamplingTime = CURRENT_SENSOR_SAMPTIME;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_10; // Battery IN
	sConfig.Rank = ADC_REGULAR_RANK_8;
	sConfig.SamplingTime = CURRENT_SENSOR_SAMPTIME;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	sConfig.Channel = ADC_CHANNEL_12; // Solar input
	sConfig.Rank = ADC_REGULAR_RANK_9;
	sConfig.SamplingTime = BATTERY_SAMPTIME;
	if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }

	// Unused channel
	// sConfig.Channel = ADC_CHANNEL_VREFINT;
	// sConfig.Rank = ADC_REGULAR_RANK_9;
	// sConfig.SamplingTime = ADC_SAMPLETIME_41CYCLES_5; // max from datasheet is 17.1us
	// if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) { Error_Handler(); }
}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};
  if(adcHandle->Instance==ADC1)
  {
    /* ADC1 clock enable */
    __HAL_RCC_ADC1_CLK_ENABLE();

    __HAL_RCC_GPIOC_CLK_ENABLE();
    __HAL_RCC_GPIOA_CLK_ENABLE();
    __HAL_RCC_GPIOB_CLK_ENABLE();
    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN10
    PC1     ------> ADC1_IN11
    PC2     ------> ADC1_IN12
    PC3     ------> ADC1_IN13
    PA1     ------> ADC1_IN1
    PC4     ------> ADC1_IN14
    PC5     ------> ADC1_IN15
    PB0     ------> ADC1_IN8
    PB1     ------> ADC1_IN9
    */
    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

    GPIO_InitStruct.Pin = GPIO_PIN_0|GPIO_PIN_1;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG;
    HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

    /* ADC1 DMA Init */
    /* ADC1 Init */
    hdma_adc1.Instance = DMA1_Channel1;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_HALFWORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
    hdma_adc1.Init.Mode = DMA_NORMAL;
    hdma_adc1.Init.Priority = DMA_PRIORITY_LOW;
    if (HAL_DMA_Init(&hdma_adc1) != HAL_OK)
    {
      Error_Handler();
    }

    __HAL_LINKDMA(adcHandle,DMA_Handle,hdma_adc1);

    /* ADC1 interrupt Init */
    HAL_NVIC_SetPriority(ADC1_2_IRQn, 0, 0);
    HAL_NVIC_EnableIRQ(ADC1_2_IRQn);
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{

  if(adcHandle->Instance==ADC1)
  {
    /* Peripheral clock disable */
    __HAL_RCC_ADC1_CLK_DISABLE();

    /**ADC1 GPIO Configuration
    PC0     ------> ADC1_IN10
    PC1     ------> ADC1_IN11
    PC2     ------> ADC1_IN12
    PC3     ------> ADC1_IN13
    PA1     ------> ADC1_IN1
    PC4     ------> ADC1_IN14
    PC5     ------> ADC1_IN15
    PB0     ------> ADC1_IN8
    PB1     ------> ADC1_IN9
    */
    HAL_GPIO_DeInit(GPIOC, GPIO_PIN_0|GPIO_PIN_1|GPIO_PIN_2|GPIO_PIN_3
                          |GPIO_PIN_4|GPIO_PIN_5);

    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_1);

    HAL_GPIO_DeInit(GPIOB, GPIO_PIN_0|GPIO_PIN_1);
  }
}
