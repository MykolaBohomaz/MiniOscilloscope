/**
 * @file    input.c
 *
 * Debounce strategy: the EXTI ISR only timestamps the falling edge. The main
 * loop accepts the press once it is DEBOUNCE_MS old *and* the pin still reads
 * low; bounces while releasing (short low glitches) fail the level check.
 */
#include "input.h"
#include <stdbool.h>
#include "board.h"
#include "tim.h"

#define DEBOUNCE_MS   15u
#define LOCKOUT_MS    120u
#define COUNTS_PER_DETENT 4

typedef struct {
    uint16_t      pin;
    GPIO_TypeDef *port;
    button_t      id;
} btn_map_t;

static const btn_map_t k_btns[] = {
    { BTN_RUN_Pin,    BTN_RUN_GPIO_Port,    BTN_RUN    },
    { BTN_Single_Pin, BTN_Single_GPIO_Port, BTN_SINGLE },
    { BTN_Menu_Pin,   BTN_Menu_GPIO_Port,   BTN_MENU   },
    { ENC_SW_Pin,     ENC_SW_GPIO_Port,     BTN_ENC    },
};
#define N_BTNS (sizeof k_btns / sizeof k_btns[0])

static volatile uint32_t s_edge_ms[N_BTNS];
static volatile uint8_t  s_pending[N_BTNS];
static uint32_t          s_last_accept_ms[N_BTNS];
static uint16_t          s_enc_last;
static int               s_enc_acc;

void input_init(void)
{
    s_enc_last = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
    s_enc_acc = 0;
}

void input_isr_exti(uint16_t pin)
{
    for (uint32_t i = 0; i < N_BTNS; i++) {
        if (k_btns[i].pin == pin) {
            if (!s_pending[i]) {
                s_edge_ms[i] = HAL_GetTick();
                s_pending[i] = 1u;
            }
            return;
        }
    }
}

button_t input_poll_button(void)
{
    const uint32_t now = HAL_GetTick();
    for (uint32_t i = 0; i < N_BTNS; i++) {
        if (!s_pending[i] || (now - s_edge_ms[i]) < DEBOUNCE_MS) {
            continue;
        }
        const bool still_low = HAL_GPIO_ReadPin(k_btns[i].port, k_btns[i].pin) == GPIO_PIN_RESET;
        s_pending[i] = 0u;
        if (still_low && (now - s_last_accept_ms[i]) >= LOCKOUT_MS) {
            s_last_accept_ms[i] = now;
            return k_btns[i].id;
        }
    }
    return BTN_NONE;
}

int input_poll_encoder(void)
{
    const uint16_t cnt = (uint16_t)__HAL_TIM_GET_COUNTER(&htim1);
    s_enc_acc += (int16_t)(uint16_t)(cnt - s_enc_last);   /* wrap-safe delta */
    s_enc_last = cnt;
    const int detents = s_enc_acc / COUNTS_PER_DETENT;    /* toward zero */
    s_enc_acc -= detents * COUNTS_PER_DETENT;
    return detents;
}
