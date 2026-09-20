/**
 * @file    ui_format.h
 * @brief   Float-free-printf number formatting (newlib-nano has no %f by
 *          default, and pulling it in costs ~10 KB of flash).
 */
#ifndef UI_FORMAT_H
#define UI_FORMAT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Engineering notation with 3 significant digits and an SI prefix:
 *   fmt_eng(b, n, 0.001234f, "V") -> "1.23mV"
 *   fmt_eng(b, n, 1000.0f,  "Hz") -> "1.00kHz"
 *   fmt_eng(b, n, -12.34f,  "V")  -> "-12.3V"
 * Non-finite values print as "---".
 * @return number of characters written (excluding NUL).
 */
size_t fmt_eng(char *dst, size_t cap, float value, const char *unit);

/** Fixed one-decimal percentage, e.g. 49.96 -> "50.0%". */
size_t fmt_pct(char *dst, size_t cap, float pct);

#ifdef __cplusplus
}
#endif
#endif /* UI_FORMAT_H */
