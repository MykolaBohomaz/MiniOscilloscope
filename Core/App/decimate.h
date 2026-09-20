/**
 * @file    decimate.h
 * @brief   Min/max (peak-detect) decimation of a record onto display columns.
 *
 * Every sample of the record lands in exactly one column and contributes to
 * that column's min and max, so a single-sample glitch is never lost the way
 * it would be with "take every Nth sample" decimation.
 */
#ifndef DECIMATE_H
#define DECIMATE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void decimate_minmax(const uint16_t *buf, uint32_t buf_len,
                     uint32_t start_abs, uint32_t n,
                     uint16_t *col_min, uint16_t *col_max, uint32_t cols);

#ifdef __cplusplus
}
#endif
#endif /* DECIMATE_H */
