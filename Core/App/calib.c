/**
 * @file    calib.c
 */
#include "calib.h"
#include "scope_config.h"

/* Nominal divider values from the roadmap (1.00 Mohm high arm, low arm to a
 * 1.65 V mid-rail):  k = Rlo / (Rhi + Rlo),  gain = 1/k,  offset = 1.65 V (1-k). */
const range_desc_t g_ranges[RANGE_COUNT] = {
    [RANGE_DIRECT] = { "3V3",  { 1.0f,    0.0f    }, false },
    [RANGE_30V]    = { "30V",  { 26.510f, 1587.8f }, true  },  /* 39.2 k low arm */
    [RANGE_50V]    = { "50V",  { 41.161f, 1609.9f }, true  },  /* 24.9 k low arm */
};

float calib_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal)
{
    if (vrefint_raw == 0u) {
        return 3300.0f;
    }
    return VREFINT_CAL_VDDA_MV * (float)vrefint_cal / (float)vrefint_raw;
}

float calib_raw_to_mv(float raw, float vdda_mv, const range_cal_t *cal)
{
    const float v_adc = raw * vdda_mv / (float)ADC_FULL_SCALE;
    return cal->gain * (v_adc - cal->offset_mv);
}

uint16_t calib_mv_to_raw(float mv, float vdda_mv, const range_cal_t *cal)
{
    const float v_adc = mv / cal->gain + cal->offset_mv;
    float raw = v_adc * (float)ADC_FULL_SCALE / vdda_mv + 0.5f;
    if (raw < 0.0f) { raw = 0.0f; }
    if (raw > (float)ADC_FULL_SCALE) { raw = (float)ADC_FULL_SCALE; }
    return (uint16_t)raw;
}
