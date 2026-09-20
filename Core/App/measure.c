/**
 * @file    measure.c
 *
 * Two passes over the record:
 *   1. min / max / sum / sum of squares (64-bit accumulators, exact).
 *   2. mid-level crossing detector with hysteresis for period and duty.
 *      Rising crossings are interpolated between the two samples that straddle
 *      the mid level, so the period estimate is sub-sample accurate.
 */
#include "measure.h"
#include "scope_config.h"
#include <math.h>
#include <string.h>

#define AT(i) (buf[((start_abs) + (i)) % buf_len])

void meas_compute(const uint16_t *buf, uint32_t buf_len,
                  uint32_t start_abs, uint32_t n, meas_t *out)
{
    memset(out, 0, sizeof *out);
    out->n = n;
    if (n == 0u) {
        return;
    }

    /* ---- pass 1: amplitude statistics ---------------------------------- */
    uint32_t mn = 0xFFFFu, mx = 0u;
    uint64_t sum = 0u, sumsq = 0u;
    uint32_t idx = start_abs % buf_len;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t s = buf[idx];
        if (++idx == buf_len) {
            idx = 0u;
        }
        if (s < mn) { mn = s; }
        if (s > mx) { mx = s; }
        sum += s;
        sumsq += (uint64_t)s * s;
    }
    out->min = (uint16_t)mn;
    out->max = (uint16_t)mx;
    out->clip_low = (mn == 0u);
    out->clip_high = (mx >= ADC_FULL_SCALE);
    const double mean = (double)sum / (double)n;
    const double msq = (double)sumsq / (double)n;
    out->mean = (float)mean;
    out->rms = (float)sqrt(msq);
    const double var = msq - mean * mean;
    out->ac_rms = (float)sqrt(var > 0.0 ? var : 0.0);

    /* ---- pass 2: period / duty via mid-level crossings -------------------- */
    const uint32_t pp = mx - mn;
    if (pp < MEAS_MIN_PP_CODES) {
        return;
    }
    const float mid = 0.5f * (float)(mn + mx);
    const float hyst = (float)pp * 0.1f;        /* +-10 % of p-p */
    const float thr_hi = mid + hyst;
    const float thr_lo = mid - hyst;

    /* A rising edge is a transition from "confirmed low" (<= thr_lo) to
     * "confirmed high" (>= thr_hi). Its time is the interpolated mid-level
     * crossing, found by walking back to the last sample below mid. */
    bool high = (float)AT(0) >= mid;
    bool low_confirmed = (float)AT(0) <= thr_lo;
    uint32_t low_entry = 0u;
    float first_rise = 0.0f, last_rise = 0.0f;
    uint32_t rises = 0u;

    for (uint32_t i = 1u; i < n; i++) {
        const float s = (float)AT(i);
        if (!high) {
            if (s <= thr_lo) {
                low_confirmed = true;
            }
            if (s >= thr_hi) {
                high = true;
                if (low_confirmed) {
                    uint32_t j = i - 1u;
                    while (j > low_entry && (float)AT(j) >= mid) {
                        j--;
                    }
                    const float y0 = (float)AT(j);
                    const float y1 = (float)AT(j + 1u);
                    if (y0 < mid && y1 >= mid) {
                        const float t = (float)j + (mid - y0) / (y1 - y0);
                        if (rises == 0u) {
                            first_rise = t;
                        }
                        last_rise = t;
                        rises++;
                    }
                }
            }
        } else if (s <= thr_lo) {
            high = false;
            low_confirmed = true;
            low_entry = i;
        }
    }

    /* Duty: share of samples >= mid over the whole periods [first, last). */
    uint32_t high_count = 0u;
    if (rises >= 2u) {
        const uint32_t a = (uint32_t)ceilf(first_rise);
        const uint32_t b = (uint32_t)ceilf(last_rise);
        for (uint32_t i = a; i < b; i++) {
            if ((float)AT(i) >= mid) {
                high_count++;
            }
        }
    }

    out->rising_edges = rises;
    if (rises >= 2u && last_rise > first_rise) {
        const float span = last_rise - first_rise;
        out->periodic = true;
        out->period = span / (float)(rises - 1u);
        float d = (float)high_count / span;
        if (d > 1.0f) { d = 1.0f; }
        if (d < 0.0f) { d = 0.0f; }
        out->duty = d;
    }
}
