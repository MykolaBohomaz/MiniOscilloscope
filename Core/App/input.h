/**
 * @file    input.h
 * @brief   Buttons (EXTI, debounced in the main loop) and rotary encoder
 *          (TIM1 hardware quadrature counter, polled).
 */
#ifndef INPUT_H
#define INPUT_H

#include <stdint.h>

typedef enum {
    BTN_NONE = 0,
    BTN_RUN,
    BTN_SINGLE,
    BTN_MENU,
    BTN_ENC,
} button_t;

void input_init(void);

/** Next debounced button press, or BTN_NONE. */
button_t input_poll_button(void);

/** Encoder movement since last call, in detents (4 counts per detent). */
int input_poll_encoder(void);

/* ISR hook: GPIO EXTI callback */
void input_isr_exti(uint16_t pin);

#endif /* INPUT_H */
