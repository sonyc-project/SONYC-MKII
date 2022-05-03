/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f1xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

//#define I_AM_REV00_BRD 1 // Build 6307, circa July 2019, small run of 10 prototypes
#define I_AM_REV01_BRD 1 // Build 6409, circa Nov 2019, has a few hardware differences

#if (I_AM_REV00_BRD && I_AM_REV01_BRD)
#error "Bad revision spec"
#endif

// Only use __BKPT() if in debug mode
#ifdef _DEBUG
#define MY_BKPT() __BKPT()
#else
#define MY_BKPT() ((void)0)
#endif

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define EN_BAL_3_Pin GPIO_PIN_13
#define EN_BAL_3_GPIO_Port GPIOC
#define RF_EN_1V8_Pin GPIO_PIN_0
#define RF_EN_1V8_GPIO_Port GPIOD
#define nFLASH_WP_Pin GPIO_PIN_4
#define nFLASH_WP_GPIO_Port GPIOA
#define INT_HEATER_Pin GPIO_PIN_10
#define INT_HEATER_GPIO_Port GPIOB
#define PG_1V8_Pin GPIO_PIN_12
#define PG_1V8_GPIO_Port GPIOB
#define PV_RUN_SET_0_Pin GPIO_PIN_13
#define PV_RUN_SET_0_GPIO_Port GPIOB
#define PV_RUN_SET_1_Pin GPIO_PIN_14
#define PV_RUN_SET_1_GPIO_Port GPIOB

// Rev01 only
#define PV_RUN_SET_2_Pin GPIO_PIN_2
#define PV_RUN_SET_2_GPIO_Port GPIOD

// Rev01 only
#define MCP_EN_Pin GPIO_PIN_11
#define MCP_EN_GPIO_Port GPIOA

#define PA0_BUTTON_Pin GPIO_PIN_0
#define PA0_BUTTON_GPIO_Port GPIOA

#define H7_BOOT_0_Pin GPIO_PIN_15
#define H7_BOOT_0_GPIO_Port GPIOB
#define nH7_RESET_Pin GPIO_PIN_6
#define nH7_RESET_GPIO_Port GPIOC
#define EN_1V8_Pin GPIO_PIN_7
#define EN_1V8_GPIO_Port GPIOC
#define VUSB_EN_FVT_Pin GPIO_PIN_8
#define VUSB_EN_FVT_GPIO_Port GPIOC
#define MODE_SET_Pin GPIO_PIN_9
#define MODE_SET_GPIO_Port GPIOC
#define VBUS_DET_Pin GPIO_PIN_8
#define VBUS_DET_GPIO_Port GPIOA
#define MPPC_SET_Pin GPIO_PIN_12
#define MPPC_SET_GPIO_Port GPIOA
#define EN_BAL_0_Pin GPIO_PIN_10
#define EN_BAL_0_GPIO_Port GPIOC
#define EN_BAL_1_Pin GPIO_PIN_11
#define EN_BAL_1_GPIO_Port GPIOC
#define EN_BAL_2_Pin GPIO_PIN_12
#define EN_BAL_2_GPIO_Port GPIOC
#define H7_UART_CTS_Pin GPIO_PIN_4
#define H7_UART_CTS_GPIO_Port GPIOB
#define H7_UART_RTS_Pin GPIO_PIN_5
#define H7_UART_RTS_GPIO_Port GPIOB
#define nLED_Pin GPIO_PIN_6
#define nLED_GPIO_Port GPIOB
#define nFLASH_RST_Pin GPIO_PIN_7
#define nFLASH_RST_GPIO_Port GPIOB
#define nFLASH_CS_Pin GPIO_PIN_8
#define nFLASH_CS_GPIO_Port GPIOB
#define EXT_HEATER_Pin GPIO_PIN_9
#define EXT_HEATER_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
