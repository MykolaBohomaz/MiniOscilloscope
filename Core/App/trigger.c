/**
 * @file    trigger.c
 */
#include "trigger.h"

bool trig_search(const uint16_t *buf, uint32_t buf_len,
                 uint32_t from_abs, uint32_t to_abs,
                 const trig_cfg_t *cfg, trig_state_t *st, uint32_t *hit_abs)
{
    uint32_t remaining = to_abs - from_abs;          /* wrap-safe */
    uint32_t idx = from_abs % buf_len;
    uint32_t abs = from_abs;
    bool armed = st->armed;

    const int32_t level = (int32_t)cfg->level;
    const int32_t hyst  = (int32_t)cfg->hyst;

    /* Arm thresholds, clamped so extreme levels stay reachable. */
    int32_t arm_lo = level - hyst;
    int32_t arm_hi = level + hyst;
    if (arm_lo < 0) { arm_lo = 0; }
    if (arm_hi > 4095) { arm_hi = 4095; }

    while (remaining > 0u) {
        /* Walk one contiguous segment of the circular buffer at a time so the
         * inner loop has no modulo. */
        uint32_t seg = buf_len - idx;
        if (seg > remaining) {
            seg = remaining;
        }
        const uint16_t *p = &buf[idx];

        if (cfg->edge == TRIG_EDGE_RISING) {
            for (uint32_t i = 0; i < seg; i++) {
                const int32_t s = (int32_t)p[i];
                if (!armed) {
                    armed = (s <= arm_lo);
                } else if (s >= level) {
                    st->armed = false;
                    *hit_abs = abs + i;
                    return true;
                }
            }
        } else {
            for (uint32_t i = 0; i < seg; i++) {
                const int32_t s = (int32_t)p[i];
                if (!armed) {
                    armed = (s >= arm_hi);
                } else if (s <= level) {
                    st->armed = false;
                    *hit_abs = abs + i;
                    return true;
                }
            }
        }
        abs += seg;
        remaining -= seg;
        idx = 0u;
    }
    st->armed = armed;
    return false;
}

float trig_interpolate(const uint16_t *buf, uint32_t buf_len,
                       uint32_t hit_abs, uint16_t level)
{
    const float y1 = (float)buf[hit_abs % buf_len];
    const float y0 = (float)buf[(hit_abs - 1u) % buf_len];
    const float dy = y1 - y0;
    if (dy == 0.0f) {
        return 0.0f;
    }
    float frac = ((float)level - y1) / dy;      /* <= 0 for a real crossing */
    if (frac > 0.0f)  { frac = 0.0f; }
    if (frac < -1.0f) { frac = -1.0f; }
    return frac;
}
