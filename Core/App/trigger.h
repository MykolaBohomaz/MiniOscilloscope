/**
 * @file    trigger.h
 * @brief   Edge trigger with hysteresis over a circular sample buffer.
 *
 * Positions are "absolute" sample counts (monotonic uint32_t, wrapping after
 * 2^32 samples). The buffer index of absolute position a is a % buf_len.
 * All arithmetic on absolute positions is wrap-safe (unsigned differences).
 *
 * Hysteresis: for a rising edge the trigger must first be *armed* by a sample
 * at or below (level - hyst); it then fires on the first sample >= level.
 * Noise smaller than the hysteresis band therefore cannot cause re-triggering.
 */
#ifndef TRIGGER_H
#define TRIGGER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { TRIG_EDGE_RISING = 0, TRIG_EDGE_FALLING = 1 } trig_edge_t;

typedef struct {
    uint16_t    level;   /**< threshold, raw ADC codes                     */
    uint16_t    hyst;    /**< hysteresis band, raw ADC codes               */
    trig_edge_t edge;
} trig_cfg_t;

typedef struct {
    bool armed;
} trig_state_t;

static inline void trig_reset(trig_state_t *st) { st->armed = false; }

/**
 * Scan absolute positions [from_abs, to_abs).
 * @return true when an edge was found; *hit_abs = first sample past the level.
 *         The state is left un-armed after a hit.
 */
bool trig_search(const uint16_t *buf, uint32_t buf_len,
                 uint32_t from_abs, uint32_t to_abs,
                 const trig_cfg_t *cfg, trig_state_t *st, uint32_t *hit_abs);

/**
 * Linear interpolation of the exact crossing between samples hit-1 and hit.
 * @return offset in samples relative to hit_abs, in (-1, 0].
 */
float trig_interpolate(const uint16_t *buf, uint32_t buf_len,
                       uint32_t hit_abs, uint16_t level);

#ifdef __cplusplus
}
#endif
#endif /* TRIGGER_H */
