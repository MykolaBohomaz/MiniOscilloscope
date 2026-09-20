/**
 * @file    scope_settings.c
 */
#include "scope_settings.h"
#include "scope_config.h"
#include "timebase.h"

const uint32_t g_gen_freq_hz[GEN_FREQ_COUNT] = { 100u, 1000u, 10000u };

void scope_settings_default(scope_settings_t *s)
{
    s->tb_index = TIMEBASE_DEFAULT_INDEX;
    s->mode = TRIG_MODE_AUTO;
    s->edge = TRIG_EDGE_RISING;
    s->trig_level = ADC_FULL_SCALE / 2u;
    s->trig_hyst = 40u;           /* ~32 mV at VDDA = 3.3 V */
    s->pretrig_pct = 50u;
    s->range = RANGE_DIRECT;
    s->gen_wave = GEN_OFF;
    s->gen_freq = 1u;
    s->backlight = 5u;
    s->running = true;
}

void scope_settings_sanitize(scope_settings_t *s)
{
    if (s->tb_index >= g_timebase_count) { s->tb_index = TIMEBASE_DEFAULT_INDEX; }
    if ((unsigned)s->mode >= (unsigned)TRIG_MODE_COUNT) { s->mode = TRIG_MODE_AUTO; }
    if ((unsigned)s->edge > (unsigned)TRIG_EDGE_FALLING) { s->edge = TRIG_EDGE_RISING; }
    if (s->trig_level > ADC_FULL_SCALE) { s->trig_level = ADC_FULL_SCALE; }
    if (s->trig_hyst > 512u) { s->trig_hyst = 512u; }
    if (s->pretrig_pct < 10u) { s->pretrig_pct = 10u; }
    if (s->pretrig_pct > 90u) { s->pretrig_pct = 90u; }
    if ((unsigned)s->range >= (unsigned)RANGE_COUNT) { s->range = RANGE_DIRECT; }
    if ((unsigned)s->gen_wave >= (unsigned)GEN_WAVE_COUNT) { s->gen_wave = GEN_OFF; }
    if (s->gen_freq >= GEN_FREQ_COUNT) { s->gen_freq = 1u; }
    if (s->backlight > 10u) { s->backlight = 10u; }
}
