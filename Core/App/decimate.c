/**
 * @file    decimate.c
 */
#include "decimate.h"

void decimate_minmax(const uint16_t *buf, uint32_t buf_len,
                     uint32_t start_abs, uint32_t n,
                     uint16_t *col_min, uint16_t *col_max, uint32_t cols)
{
    uint32_t idx = start_abs % buf_len;
    uint32_t consumed = 0u;

    for (uint32_t c = 0; c < cols; c++) {
        /* Samples [c*n/cols, (c+1)*n/cols) belong to column c. */
        const uint32_t end = (uint32_t)(((uint64_t)(c + 1u) * n) / cols);
        uint16_t lo = 0xFFFFu, hi = 0u;

        if (end == consumed) {
            /* Fewer samples than columns: repeat the nearest sample. */
            const uint32_t k = (consumed == 0u) ? 0u : consumed - 1u;
            const uint16_t s = buf[(start_abs + k) % buf_len];
            col_min[c] = s;
            col_max[c] = s;
            continue;
        }
        while (consumed < end) {
            const uint16_t s = buf[idx];
            if (++idx == buf_len) {
                idx = 0u;
            }
            if (s < lo) { lo = s; }
            if (s > hi) { hi = s; }
            consumed++;
        }
        col_min[c] = lo;
        col_max[c] = hi;
    }
}
