/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define TEST_PT_PC15_Pin GPIO_PIN_15
#define TEST_PT_PC15_GPIO_Port GPIOC
#define MUX_EN_Pin GPIO_PIN_0
#define MUX_EN_GPIO_Port GPIOD
#define EN_2V8_Pin GPIO_PIN_3
#define EN_2V8_GPIO_Port GPIOC
#define CHG_BTN_Pin GPIO_PIN_0
#define CHG_BTN_GPIO_Port GPIOA
#define CHG_BTN_EXTI_IRQn EXTI0_IRQn
#define DCHG_A0_Pin GPIO_PIN_4
#define DCHG_A0_GPIO_Port GPIOC
#define DCHG_A1_Pin GPIO_PIN_6
#define DCHG_A1_GPIO_Port GPIOC
#define LED_RED_Pin GPIO_PIN_0
#define LED_RED_GPIO_Port GPIOB
#define LED_GREEN_Pin GPIO_PIN_1
#define LED_GREEN_GPIO_Port GPIOB
#define CHG_A2_Pin GPIO_PIN_10
#define CHG_A2_GPIO_Port GPIOB
#define CHG_A3_Pin GPIO_PIN_11
#define CHG_A3_GPIO_Port GPIOB
#define CHG_B0_Pin GPIO_PIN_12
#define CHG_B0_GPIO_Port GPIOB
#define CHG_B1_Pin GPIO_PIN_13
#define CHG_B1_GPIO_Port GPIOB
#define CHG_B2_Pin GPIO_PIN_14
#define CHG_B2_GPIO_Port GPIOB
#define CHG_B3_Pin GPIO_PIN_15
#define CHG_B3_GPIO_Port GPIOB
#define DCHG_A2_Pin GPIO_PIN_5
#define DCHG_A2_GPIO_Port GPIOC
#define DCHG_A3_Pin GPIO_PIN_7
#define DCHG_A3_GPIO_Port GPIOC
#define DCHG_B0_Pin GPIO_PIN_8
#define DCHG_B0_GPIO_Port GPIOC
#define DCHG_B1_Pin GPIO_PIN_10
#define DCHG_B1_GPIO_Port GPIOC
#define DISCHG_BTN_Pin GPIO_PIN_10
#define DISCHG_BTN_GPIO_Port GPIOA
#define DISCHG_BTN_EXTI_IRQn EXTI15_10_IRQn
#define DCHG_B2_Pin GPIO_PIN_9
#define DCHG_B2_GPIO_Port GPIOC
#define DCHG_B3_Pin GPIO_PIN_11
#define DCHG_B3_GPIO_Port GPIOC
#define MUX_0_Pin GPIO_PIN_5
#define MUX_0_GPIO_Port GPIOB
#define MUX_1_Pin GPIO_PIN_6
#define MUX_1_GPIO_Port GPIOB
#define MUX_2_Pin GPIO_PIN_7
#define MUX_2_GPIO_Port GPIOB
#define CHG_A0_Pin GPIO_PIN_8
#define CHG_A0_GPIO_Port GPIOB
#define CHG_A1_Pin GPIO_PIN_9
#define CHG_A1_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
