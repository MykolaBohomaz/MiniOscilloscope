/**
 * @file    board.h
 * @brief   Board abstraction: every pin the application touches is named here,
 *          mapped onto the CubeMX user labels from main.h. Moving from the
 *          NUCLEO-L432KC to the custom PCB should only change this file and the
 *          .ioc, never application logic (handoff section 11.2, item 11).
 */
#ifndef BOARD_H
#define BOARD_H

#include "main.h"

/* Timing probes (PA11 / PA12 on Nucleo header D10 / D2). Single-cycle BSRR
 * writes so they can bracket code with negligible overhead.
 *   DBG1: high while a record is processed (measure + decimate)
 *   DBG2: toggles on every ADC DMA half/full event -> square wave at
 *         Fs / ACQ_BUF_LEN, used to measure the real sample rate externally. */
#define DBG1_HIGH()    (DBG_Timing_GPIO_Port->BSRR = DBG_Timing_Pin)
#define DBG1_LOW()     (DBG_Timing_GPIO_Port->BRR  = DBG_Timing_Pin)
#define DBG2_TOGGLE()  (DBG_Timing2_GPIO_Port->ODR ^= DBG_Timing2_Pin)

#define LED_ON()       (LD3_GPIO_Port->BSRR = LD3_Pin)
#define LED_OFF()      (LD3_GPIO_Port->BRR  = LD3_Pin)
#define LED_TOGGLE()   (LD3_GPIO_Port->ODR ^= LD3_Pin)

#define LCD_CS_HIGH()  (LCD_CS_GPIO_Port->BSRR = LCD_CS_Pin)
#define LCD_CS_LOW()   (LCD_CS_GPIO_Port->BRR  = LCD_CS_Pin)
#define LCD_A0_DATA()  (LCD_A0_GPIO_Port->BSRR = LCD_A0_Pin)
#define LCD_A0_CMD()   (LCD_A0_GPIO_Port->BRR  = LCD_A0_Pin)
#define LCD_RST_HIGH() (LCD_RST_GPIO_Port->BSRR = LCD_RST_Pin)
#define LCD_RST_LOW()  (LCD_RST_GPIO_Port->BRR  = LCD_RST_Pin)

/* Buttons are active low with pull-ups, EXTI on the falling edge. */
#define BTN_RUN_PIN     BTN_RUN_Pin
#define BTN_SINGLE_PIN  BTN_Single_Pin
#define BTN_MENU_PIN    BTN_Menu_Pin
#define BTN_ENC_PIN     ENC_SW_Pin

/* Backlight PWM: TIM2 CH4 on PA3, ARR = 3999 (20 kHz). */
#define BACKLIGHT_PWM_MAX 3999u

#endif /* BOARD_H */
