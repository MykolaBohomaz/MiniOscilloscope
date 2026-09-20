/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
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
#include "stm32l4xx_hal.h"

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
#define VCP_TX_Pin GPIO_PIN_2
#define VCP_TX_GPIO_Port GPIOA
#define LCD_CS_Pin GPIO_PIN_0
#define LCD_CS_GPIO_Port GPIOB
#define LCD_A0_Pin GPIO_PIN_1
#define LCD_A0_GPIO_Port GPIOB
#define ENC_SW_Pin GPIO_PIN_10
#define ENC_SW_GPIO_Port GPIOA
#define ENC_SW_EXTI_IRQn EXTI15_10_IRQn
#define DBG_Timing_Pin GPIO_PIN_11
#define DBG_Timing_GPIO_Port GPIOA
#define DBG_Timing2_Pin GPIO_PIN_12
#define DBG_Timing2_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define VCP_RX_Pin GPIO_PIN_15
#define VCP_RX_GPIO_Port GPIOA
#define LD3_Pin GPIO_PIN_3
#define LD3_GPIO_Port GPIOB
#define LCD_RST_Pin GPIO_PIN_4
#define LCD_RST_GPIO_Port GPIOB
#define BTN_RUN_Pin GPIO_PIN_5
#define BTN_RUN_GPIO_Port GPIOB
#define BTN_RUN_EXTI_IRQn EXTI9_5_IRQn
#define BTN_Single_Pin GPIO_PIN_6
#define BTN_Single_GPIO_Port GPIOB
#define BTN_Single_EXTI_IRQn EXTI9_5_IRQn
#define BTN_Menu_Pin GPIO_PIN_7
#define BTN_Menu_GPIO_Port GPIOB
#define BTN_Menu_EXTI_IRQn EXTI9_5_IRQn

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
