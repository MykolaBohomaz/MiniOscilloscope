/**
 * @file    ui_menu.c
 */
#include "ui_menu.h"
#include "scope_config.h"
#include "timebase.h"

static int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

uint32_t ui_menu_adjust(scope_settings_t *s, menu_item_t sel, int d)
{
    if (d == 0) {
        return CHG_NONE;
    }
    switch (sel) {
    case MENU_TIMEBASE: {
        const int v = clampi((int)s->tb_index + d, 0, (int)g_timebase_count - 1);
        if (v == (int)s->tb_index) { return CHG_NONE; }
        s->tb_index = (uint8_t)v;
        return CHG_TIMEBASE;
    }
    case MENU_TRIG_LEVEL: {
        const int v = clampi((int)s->trig_level + d * (int)UI_TRIG_STEP_CODES, 0, (int)ADC_FULL_SCALE);
        if (v == (int)s->trig_level) { return CHG_NONE; }
        s->trig_level = (uint16_t)v;
        return CHG_TRIGGER;
    }
    case MENU_EDGE:
        s->edge = (s->edge == TRIG_EDGE_RISING) ? TRIG_EDGE_FALLING : TRIG_EDGE_RISING;
        return CHG_TRIGGER;
    case MENU_MODE: {
        /* SINGLE is reached with its own button; the menu toggles AUTO/NORMAL. */
        s->mode = (s->mode == TRIG_MODE_AUTO) ? TRIG_MODE_NORMAL : TRIG_MODE_AUTO;
        return CHG_TRIGGER;
    }
    case MENU_PRETRIG: {
        const int v = clampi((int)s->pretrig_pct + 10 * d, 10, 90);
        if (v == (int)s->pretrig_pct) { return CHG_NONE; }
        s->pretrig_pct = (uint8_t)v;
        return CHG_TRIGGER;
    }
    case MENU_GEN: {
        /* Walk OFF -> SINE f0 -> SINE f1 -> ... -> TRIANGLE f2. */
        const int total = 1 + (int)(GEN_WAVE_COUNT - 1) * (int)GEN_FREQ_COUNT;
        int pos = (s->gen_wave == GEN_OFF) ? 0
                : 1 + ((int)s->gen_wave - 1) * (int)GEN_FREQ_COUNT + (int)s->gen_freq;
        pos = clampi(pos + d, 0, total - 1);
        if (pos == 0) {
            s->gen_wave = GEN_OFF;
        } else {
            s->gen_wave = (gen_wave_t)(1 + (pos - 1) / (int)GEN_FREQ_COUNT);
            s->gen_freq = (uint8_t)((pos - 1) % (int)GEN_FREQ_COUNT);
        }
        return CHG_GEN;
    }
    case MENU_BACKLIGHT: {
        const int v = clampi((int)s->backlight + d, 0, 10);
        if (v == (int)s->backlight) { return CHG_NONE; }
        s->backlight = (uint8_t)v;
        return CHG_BACKLIGHT;
    }
    default:
        return CHG_NONE;
    }
}

const char *ui_menu_name(menu_item_t m)
{
    static const char *const names[MENU_COUNT] = {
        "TIME/DIV", "TRIG LVL", "EDGE", "MODE", "PRE-TRIG", "SIG GEN", "BACKLIGHT"
    };
    return (m < MENU_COUNT) ? names[m] : "?";
}
