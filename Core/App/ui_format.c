/**
 * @file    ui_format.c
 */
#include "ui_format.h"
#include <math.h>
#include <stdio.h>

static const char k_prefix[] = { 'p', 'n', 'u', 'm', 0, 'k', 'M', 'G' };
#define PREFIX_UNITY 4

size_t fmt_eng(char *dst, size_t cap, float value, const char *unit)
{
    int n;
    if (!isfinite(value)) {
        n = snprintf(dst, cap, "---");
        return n < 0 ? 0u : (size_t)n;
    }
    const int neg = value < 0.0f;
    double a = fabs((double)value);
    int p = PREFIX_UNITY;

    if (a != 0.0) {
        while (a >= 1000.0 && p < (int)sizeof k_prefix - 1) { a /= 1000.0; p++; }
        while (a < 1.0 && p > 0) { a *= 1000.0; p--; }
    }

    /* 3 significant digits; rounding may carry into the next decade. */
    int dec = (a < 10.0) ? 2 : (a < 100.0) ? 1 : 0;
    long scaled;
    for (;;) {
        const double mul = (dec == 2) ? 100.0 : (dec == 1) ? 10.0 : 1.0;
        scaled = (long)(a * mul + 0.5);
        if (scaled < 1000L) {
            break;
        }
        if (dec > 0) {
            dec--;
        } else if (p < (int)sizeof k_prefix - 1) {
            a /= 1000.0;
            p++;
            dec = 2;
        } else {
            break;
        }
    }

    const char pre[2] = { k_prefix[p], 0 };
    if (dec == 0) {
        n = snprintf(dst, cap, "%s%ld%s%s", neg ? "-" : "", scaled, pre, unit);
    } else {
        const long div = (dec == 2) ? 100L : 10L;
        n = snprintf(dst, cap, "%s%ld.%0*ld%s%s", neg ? "-" : "",
                     scaled / div, dec, scaled % div, pre, unit);
    }
    return n < 0 ? 0u : (size_t)n;
}

size_t fmt_pct(char *dst, size_t cap, float pct)
{
    int n;
    if (!isfinite(pct)) {
        n = snprintf(dst, cap, "---");
    } else {
        long t = (long)lroundf(pct * 10.0f);
        const char *sign = (t < 0) ? "-" : "";
        if (t < 0) { t = -t; }
        n = snprintf(dst, cap, "%s%ld.%ld%%", sign, t / 10, t % 10);
    }
    return n < 0 ? 0u : (size_t)n;
}
