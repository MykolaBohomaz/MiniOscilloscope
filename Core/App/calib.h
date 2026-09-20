/**
 * @file    calib.h
 * @brief   Raw ADC code <-> input voltage conversion per input range.
 *
 *   V_adc = code * VDDA / 4095              (VREF+ is tied to VDDA on UFQFPN32)
 *   V_in  = gain * (V_adc - offset)
 *
 * VDDA is not assumed to be 3.300 V: it is derived from the factory VREFINT
 * calibration word (measured by ST at VDDA = 3.0 V) and a VREFINT conversion.
 *
 * Range gains/offsets for the attenuator ranges are the *nominal* values of
 * the planned divider (roadmap section 3.1) and are placeholders until each
 * range is calibrated; the Nucleo prototype only supports RANGE_DIRECT.
 */
#ifndef CALIB_H
#define CALIB_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RANGE_DIRECT = 0,   /**< PA0 driven directly, 0 .. VDDA (Nucleo)          */
    RANGE_30V,          /**< +-30 V attenuator (front end Rev A, not built)    */
    RANGE_50V,          /**< +-50 V attenuator (front end Rev A, not built)    */
    RANGE_COUNT
} range_id_t;

typedef struct {
    float gain;        /**< V_in per V_adc                                    */
    float offset_mv;   /**< V_adc that corresponds to V_in = 0                */
} range_cal_t;

typedef struct {
    const char *name;
    range_cal_t nominal;
    bool        needs_front_end;
} range_desc_t;

extern const range_desc_t g_ranges[RANGE_COUNT];

/** Factory VREFINT calibration voltage (STM32L4: VDDA = 3.0 V during test). */
#define VREFINT_CAL_VDDA_MV 3000.0f

float calib_vdda_mv(uint16_t vrefint_raw, uint16_t vrefint_cal);
float calib_raw_to_mv(float raw, float vdda_mv, const range_cal_t *cal);
uint16_t calib_mv_to_raw(float mv, float vdda_mv, const range_cal_t *cal);

#ifdef __cplusplus
}
#endif
#endif /* CALIB_H */
