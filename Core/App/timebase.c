/**
 * @file    timebase.c
 */
#include "timebase.h"
#include "scope_config.h"
#include <stdio.h>

static const uint16_t k_smp_x2[SMP_COUNT] = { 5u, 13u, 25u, 49u, 95u, 185u, 495u, 1281u };

float tb_sample_rate_hz(const timebase_t *tb)
{
    return (float)SCOPE_TIMER_CLK_HZ / (float)tb_period_cycles(tb);
}

uint32_t tb_smp_cycles_x2(adc_smp_t smp)
{
    return (smp < SMP_COUNT) ? k_smp_x2[smp] : 0u;
}

bool tb_is_valid(const timebase_t *tb)
{
    if (tb->smp >= SMP_COUNT) {
        return false;
    }
    /* ADC clock == timer clock (both 80 MHz), so cycles compare directly:
     * sampling + 12.5 conversion cycles must fit in one trigger period. */
    const uint32_t need_x2 = k_smp_x2[tb->smp] + SCOPE_ADC_CONV_CYCLES_X2;
    if (need_x2 > 2u * tb_period_cycles(tb)) {
        return false;
    }
    return (tb->record_len >= LCD_WIDTH) && (tb->record_len <= ACQ_RECORD_MAX);
}

size_t tb_format_div(char *dst, size_t cap, uint32_t ns)
{
    int n;
    if (ns >= 1000000000u && ns % 1000000000u == 0u) {
        n = snprintf(dst, cap, "%lus", (unsigned long)(ns / 1000000000u));
    } else if (ns >= 1000000u && ns % 1000000u == 0u) {
        n = snprintf(dst, cap, "%lums", (unsigned long)(ns / 1000000u));
    } else if (ns >= 1000u && ns % 1000u == 0u) {
        n = snprintf(dst, cap, "%luus", (unsigned long)(ns / 1000u));
    } else {
        n = snprintf(dst, cap, "%luns", (unsigned long)ns);
    }
    return (n < 0) ? 0u : (size_t)n;
}
