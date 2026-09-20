/**
 * @file    unit.h
 * @brief   Tiny single-header unit test harness (no dependencies).
 */
#ifndef UNIT_H
#define UNIT_H

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int g_unit_failures;
static int g_unit_checks;

#define CHECK(cond) do {                                                     \
    g_unit_checks++;                                                         \
    if (!(cond)) {                                                           \
        g_unit_failures++;                                                   \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
    }                                                                        \
} while (0)

#define CHECK_EQ_U(a, b) do {                                                \
    unsigned long long _a = (unsigned long long)(a), _b = (unsigned long long)(b); \
    g_unit_checks++;                                                         \
    if (_a != _b) {                                                          \
        g_unit_failures++;                                                   \
        fprintf(stderr, "  FAIL %s:%d: %s == %s (%llu vs %llu)\n",           \
                __FILE__, __LINE__, #a, #b, _a, _b);                         \
    }                                                                        \
} while (0)

#define CHECK_NEAR(a, b, tol) do {                                           \
    double _a = (double)(a), _b = (double)(b);                               \
    g_unit_checks++;                                                         \
    if (!(fabs(_a - _b) <= (double)(tol))) {                                 \
        g_unit_failures++;                                                   \
        fprintf(stderr, "  FAIL %s:%d: %s ~= %s (%.6g vs %.6g, tol %.3g)\n", \
                __FILE__, __LINE__, #a, #b, _a, _b, (double)(tol));          \
    }                                                                        \
} while (0)

#define CHECK_STR(a, b) do {                                                 \
    const char *_a = (a), *_b = (b);                                         \
    g_unit_checks++;                                                         \
    if (strcmp(_a, _b) != 0) {                                               \
        g_unit_failures++;                                                   \
        fprintf(stderr, "  FAIL %s:%d: \"%s\" == \"%s\"\n",                  \
                __FILE__, __LINE__, _a, _b);                                 \
    }                                                                        \
} while (0)

#define RUN(test) do { printf("  %s\n", #test); test(); } while (0)

#define UNIT_REPORT() do {                                                   \
    printf("%d checks, %d failures\n", g_unit_checks, g_unit_failures);      \
    return g_unit_failures ? EXIT_FAILURE : EXIT_SUCCESS;                    \
} while (0)

#endif /* UNIT_H */
