/**
 * @file    measure.h
 * @brief   Automatic measurements on a frozen record, in raw ADC units.
 *
 * Results are in ADC codes and sample periods; conversion to volts and
 * seconds (calibration, sample rate) happens in the caller so this module
 * stays exact and host-testable.
 */
#ifndef MEASURE_H
#define MEASURE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t n;              /**< samples analysed                             */
    uint16_t min;            /**< minimum code                                 */
    uint16_t max;            /**< maximum code                                 */
    float    mean;           /**< average code                                 */
    float    rms;            /**< RMS of the codes (DC + AC)                   */
    float    ac_rms;         /**< RMS with the mean removed                    */
    bool     clip_low;       /**< at least one sample at code 0                */
    bool     clip_high;      /**< at least one sample at full scale            */
    bool     periodic;       /**< >= 2 rising mid-level crossings found        */
    uint32_t rising_edges;
    float    period;         /**< samples per period (valid if periodic)       */
    float    duty;           /**< 0..1 high-time share over whole periods      */
} meas_t;

/** Minimum peak-to-peak (codes) before frequency / duty are attempted. */
#define MEAS_MIN_PP_CODES 24u

void meas_compute(const uint16_t *buf, uint32_t buf_len,
                  uint32_t start_abs, uint32_t n, meas_t *out);

#ifdef __cplusplus
}
#endif
#endif /* MEASURE_H */
