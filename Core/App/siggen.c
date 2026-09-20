/**
 * @file    siggen.c
 */
#include "siggen.h"
#include "dac.h"
#include "scope_config.h"
#include "tim.h"
#include <math.h>

static uint16_t s_table[SIGGEN_POINTS];

static void build_table(gen_wave_t w, float vdda_mv)
{
    const float lo = (float)SIGGEN_LO_MV * (float)ADC_FULL_SCALE / vdda_mv;
    const float hi = (float)SIGGEN_HI_MV * (float)ADC_FULL_SCALE / vdda_mv;
    const float mid = 0.5f * (lo + hi), amp = 0.5f * (hi - lo);
    for (uint32_t i = 0; i < SIGGEN_POINTS; i++) {
        const float ph = (float)i / (float)SIGGEN_POINTS;          /* 0..1 */
        float v;
        switch (w) {
        case GEN_SQUARE:   v = (ph < 0.5f) ? hi : lo; break;
        case GEN_TRIANGLE: v = (ph < 0.5f) ? lo + (hi - lo) * 2.0f * ph
                                           : hi - (hi - lo) * 2.0f * (ph - 0.5f); break;
        case GEN_SINE:
        default:           v = mid + amp * sinf(6.2831853f * ph); break;
        }
        s_table[i] = (uint16_t)(v + 0.5f);
    }
}

void siggen_apply(gen_wave_t wave, uint8_t freq_index, float vdda_mv)
{
    (void)HAL_TIM_Base_Stop(&htim7);
    (void)HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
    if (wave == GEN_OFF || freq_index >= GEN_FREQ_COUNT) {
        return;
    }
    build_table(wave, vdda_mv);

    /* update rate = f * points; TIM7 ARR = 80 MHz / rate - 1 */
    const uint32_t rate = g_gen_freq_hz[freq_index] * SIGGEN_POINTS;
    __HAL_TIM_SET_AUTORELOAD(&htim7, SCOPE_TIMER_CLK_HZ / rate - 1u);
    __HAL_TIM_SET_COUNTER(&htim7, 0u);
    (void)HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1, (uint32_t *)s_table,
                            SIGGEN_POINTS, DAC_ALIGN_12B_R);
    (void)HAL_TIM_Base_Start(&htim7);
}
