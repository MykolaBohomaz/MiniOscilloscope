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

/* Backlight PWM: TIM2 CH4 on PA3, ARR = 3999 (20 kHz).
 *
 * The ERM19296-1 brings out only LEDA (pin 5); the backlight cathode is tied
 * to the module's VSS internally, so the backlight has to be switched on the
 * HIGH side - a P-channel MOSFET from +3V3 to LEDA, gate driven by PA3 with a
 * pull-up to +3V3 so it stays off while the pin is still an input. A P-FET
 * conducts when its gate is LOW, hence the inverted duty cycle below.
 * Set BACKLIGHT_ACTIVE_LOW to 0 if you drive the gate through an inverting
 * stage (or use a module that breaks out LEDK for low-side switching). */
#define BACKLIGHT_PWM_MAX     3999u
#define BACKLIGHT_ACTIVE_LOW  1

/** Convert 0..10 brightness into a TIM2 CH4 compare value. */
static inline uint32_t backlight_compare(uint32_t level_0_10)
{
    const uint32_t on = (BACKLIGHT_PWM_MAX + 1u) * level_0_10 / 10u;
#if BACKLIGHT_ACTIVE_LOW
    return (BACKLIGHT_PWM_MAX + 1u) - on;
#else
    return on;
#endif
}

#endif /* BOARD_H */
