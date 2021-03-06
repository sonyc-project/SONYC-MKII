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
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
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
#include "stm32h7xx_hal.h"

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
#define SRAM_CE2_Pin GPIO_PIN_5
#define SRAM_CE2_GPIO_Port GPIOE
#define MIC_OFF_Pin GPIO_PIN_0
#define MIC_OFF_GPIO_Port GPIOC
#define R_NSS_Pin GPIO_PIN_1
#define R_NSS_GPIO_Port GPIOC
#define UART2_CTS_Pin GPIO_PIN_0
#define UART2_CTS_GPIO_Port GPIOA
#define UART2_RTS_Pin GPIO_PIN_1
#define UART2_RTS_GPIO_Port GPIOA
#define LED0_Pin GPIO_PIN_0
#define LED0_GPIO_Port GPIOB
#define LED1_Pin GPIO_PIN_1
#define LED1_GPIO_Port GPIOB
#define LED2_Pin GPIO_PIN_2
#define LED2_GPIO_Port GPIOB
#define EXT_CS_Pin GPIO_PIN_11
#define EXT_CS_GPIO_Port GPIOB
#define ACCEL_CS_Pin GPIO_PIN_6
#define ACCEL_CS_GPIO_Port GPIOG
#define ACCEL_INT1_Pin GPIO_PIN_7
#define ACCEL_INT1_GPIO_Port GPIOG
#define ACCEL_INT2_Pin GPIO_PIN_8
#define ACCEL_INT2_GPIO_Port GPIOG
#define R_RESET_Pin GPIO_PIN_8
#define R_RESET_GPIO_Port GPIOA
#define DIO0_Pin GPIO_PIN_5
#define DIO0_GPIO_Port GPIOB
#define DIO1_Pin GPIO_PIN_6
#define DIO1_GPIO_Port GPIOB
#define DIO2_Pin GPIO_PIN_7
#define DIO2_GPIO_Port GPIOB
#define DIO3_Pin GPIO_PIN_8
#define DIO3_GPIO_Port GPIOB
#define DIO4_Pin GPIO_PIN_9
#define DIO4_GPIO_Port GPIOB
/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
