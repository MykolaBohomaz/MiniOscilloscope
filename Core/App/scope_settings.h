/**
 * @file    scope_settings.h
 * @brief   User-adjustable instrument settings (UI, PC link and flash share it).
 */
#ifndef SCOPE_SETTINGS_H
#define SCOPE_SETTINGS_H

#include <stdbool.h>
#include <stdint.h>
#include "acq_engine.h"
#include "calib.h"
#include "trigger.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GEN_OFF = 0, GEN_SINE, GEN_SQUARE, GEN_TRIANGLE, GEN_WAVE_COUNT } gen_wave_t;

#define GEN_FREQ_COUNT 3u
extern const uint32_t g_gen_freq_hz[GEN_FREQ_COUNT];   /* 100 Hz, 1 kHz, 10 kHz */

typedef struct {
    uint8_t     tb_index;
    trig_mode_t mode;
    trig_edge_t edge;
    uint16_t    trig_level;    /**< raw ADC code                         */
    uint16_t    trig_hyst;     /**< raw ADC codes                        */
    uint8_t     pretrig_pct;   /**< 10..90                               */
    range_id_t  range;
    gen_wave_t  gen_wave;
    uint8_t     gen_freq;      /**< index into g_gen_freq_hz             */
    uint8_t     backlight;     /**< 0..10 (x10 %)                        */
    bool        running;       /**< RUN/STOP                             */
} scope_settings_t;

void scope_settings_default(scope_settings_t *s);
/** Clamp every field into range (used after loading from flash / PC). */
void scope_settings_sanitize(scope_settings_t *s);

#ifdef __cplusplus
}
#endif
#endif /* SCOPE_SETTINGS_H */
