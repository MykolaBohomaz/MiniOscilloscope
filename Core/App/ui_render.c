/**
 * @file    ui_render.c
 */
#include "ui_render.h"
#include "gfx.h"
#include "scope_config.h"
#include "timebase.h"
#include "ui_format.h"
#include <stdio.h>
#include <string.h>

#define PLOT_BOTTOM ((int)(UI_PLOT_Y + UI_PLOT_H) - 1)

static int raw_to_y(uint32_t raw)
{
    if (raw > ADC_FULL_SCALE) { raw = ADC_FULL_SCALE; }
    return PLOT_BOTTOM - (int)((raw * (UI_PLOT_H - 1u) + ADC_FULL_SCALE / 2u) / ADC_FULL_SCALE);
}

static void draw_grid(uint8_t *fb)
{
    const int x0 = (int)UI_PLOT_X, y0 = (int)UI_PLOT_Y;
    const int w = (int)UI_PLOT_W, h = (int)UI_PLOT_H;
    const int dx = w / (int)UI_HDIVS, dy = h / (int)UI_VDIVS;

    /* Dotted division lines, dots every 4 px (every 2 px on the centre axes). */
    for (int i = 1; i < (int)UI_HDIVS; i++) {
        const int step = (i == (int)UI_HDIVS / 2) ? 2 : 4;
        for (int y = y0; y < y0 + h; y += step) {
            gfx_pixel(fb, x0 + i * dx, y, GFX_SET);
        }
    }
    for (int j = 1; j < (int)UI_VDIVS; j++) {
        const int step = (j == (int)UI_VDIVS / 2) ? 2 : 4;
        for (int x = x0; x < x0 + w; x += step) {
            gfx_pixel(fb, x, y0 + j * dy, GFX_SET);
        }
    }
    /* Frame */
    gfx_hline(fb, x0, x0 + w - 1, y0 - 1, GFX_SET);
    gfx_hline(fb, x0, x0 + w - 1, y0 + h, GFX_SET);
}

static void draw_trace(uint8_t *fb, const ui_view_t *v)
{
    int prev_lo = -1, prev_hi = -1;
    for (int x = 0; x < (int)LCD_WIDTH; x++) {
        /* larger raw -> smaller y */
        int y_hi = raw_to_y(v->col_max[x]);
        int y_lo = raw_to_y(v->col_min[x]);
        /* Connect to the previous column so steep edges stay continuous. */
        if (prev_lo >= 0) {
            if (y_hi > prev_lo) { y_hi = prev_lo; }
            if (y_lo < prev_hi) { y_lo = prev_hi; }
        }
        gfx_vline(fb, x, y_hi, y_lo, GFX_SET);
        prev_lo = raw_to_y(v->col_min[x]);
        prev_hi = raw_to_y(v->col_max[x]);
    }
}

static void draw_trigger_marks(uint8_t *fb, const ui_view_t *v)
{
    /* Level: small arrow at the right edge. */
    const int y = raw_to_y(v->set->trig_level);
    for (int i = 0; i < 3; i++) {
        gfx_vline(fb, (int)LCD_WIDTH - 1 - i, y - i, y + i, GFX_XOR);
    }
    /* Position: small down-pointing tick under the top frame. */
    const int x = (int)v->trig_col;
    gfx_hline(fb, x - 2, x + 2, (int)UI_PLOT_Y, GFX_SET);
    gfx_hline(fb, x - 1, x + 1, (int)UI_PLOT_Y + 1, GFX_SET);
    gfx_pixel(fb, x, (int)UI_PLOT_Y + 2, GFX_SET);
}

static const char *run_label(const ui_view_t *v)
{
    if (v->set->mode == TRIG_MODE_SINGLE) {
        return (v->acq_state == ACQ_STOPPED) ? "HOLD" : "SGL ";
    }
    return v->set->running ? "RUN " : "STOP";
}

static const char *trig_label(const ui_view_t *v)
{
    switch (v->acq_state) {
    case ACQ_ARMED:    return "Wait";
    case ACQ_POSTTRIG: return "Trig";
    case ACQ_READY:    return v->last_forced ? "Auto" : "Trig";
    default:           return v->last_forced ? "Auto" : "Trig";
    }
}

static void draw_status(uint8_t *fb, const ui_view_t *v)
{
    char buf[16];
    int x = 0;
    x = gfx_text(fb, x, 0, run_label(v), false) + 3;

    tb_format_div(buf, sizeof buf, g_timebases[v->set->tb_index].ns_per_div);
    strncat(buf, "/", sizeof buf - strlen(buf) - 1u);
    x = gfx_text(fb, x, 0, buf, v->sel == MENU_TIMEBASE) + 3;

    x = gfx_text(fb, x, 0, v->set->edge == TRIG_EDGE_RISING ? GLYPH_RISING : GLYPH_FALLING,
                 v->sel == MENU_EDGE);
    fmt_eng(buf, sizeof buf, v->trig_level_mv / 1000.0f, "V");
    x = gfx_text(fb, x, 0, buf, v->sel == MENU_TRIG_LEVEL) + 3;

    static const char *const modes[TRIG_MODE_COUNT] = { "AUTO", "NORM", "SNGL" };
    x = gfx_text(fb, x, 0, modes[v->set->mode], v->sel == MENU_MODE) + 3;

    if (x + 4 * GFX_CHAR_W <= (int)LCD_WIDTH) {
        gfx_text(fb, (int)LCD_WIDTH - 4 * GFX_CHAR_W, 0, trig_label(v), false);
    }
}

static void draw_meas(uint8_t *fb, const ui_view_t *v)
{
    char line[64], a[16], b[16], c[16];
    if (!v->meas_valid) {
        gfx_text(fb, 0, (int)UI_MEAS_Y0, "no record", false);
    } else {
        fmt_eng(a, sizeof a, v->vpp_mv / 1000.0f, "V");
        fmt_eng(b, sizeof b, v->vavg_mv / 1000.0f, "V");
        if (v->periodic) {
            fmt_eng(c, sizeof c, v->freq_hz, "Hz");
        } else {
            snprintf(c, sizeof c, "---");
        }
        snprintf(line, sizeof line, "PP %-6s AV %-6s F %s", a, b, c);
        gfx_text(fb, 0, (int)UI_MEAS_Y0, line, false);

        fmt_eng(a, sizeof a, v->vrms_mv / 1000.0f, "V");
        if (v->periodic) {
            fmt_pct(b, sizeof b, v->duty_pct);
        } else {
            snprintf(b, sizeof b, "---");
        }
        snprintf(line, sizeof line, "RMS %-6s D %s", a, b);
        gfx_text(fb, 0, (int)UI_MEAS_Y1, line, false);
    }

    /* Right side of row 2: the auxiliary setting (inverted when selected). */
    const scope_settings_t *s = v->set;
    menu_item_t aux = v->sel;
    if (aux != MENU_PRETRIG && aux != MENU_GEN && aux != MENU_BACKLIGHT) {
        aux = (s->gen_wave != GEN_OFF) ? MENU_GEN : MENU_PRETRIG;
    }
    static const char *const waves[GEN_WAVE_COUNT] = { "OFF", "SIN", "SQR", "TRI" };
    switch (aux) {
    case MENU_PRETRIG:
        snprintf(line, sizeof line, "PRE %u%%", (unsigned)s->pretrig_pct);
        break;
    case MENU_GEN:
        if (s->gen_wave == GEN_OFF) {
            snprintf(line, sizeof line, "GEN OFF");
        } else {
            fmt_eng(a, sizeof a, (float)g_gen_freq_hz[s->gen_freq], "");
            snprintf(line, sizeof line, "%s %s", waves[s->gen_wave], a);
        }
        break;
    default:
        snprintf(line, sizeof line, "BL %u%%", (unsigned)s->backlight * 10u);
        break;
    }
    const int w = (int)strlen(line) * GFX_CHAR_W;
    gfx_text(fb, (int)LCD_WIDTH - w, (int)UI_MEAS_Y1, line, v->sel == aux);
}

void ui_render(uint8_t *fb, const ui_view_t *v)
{
    gfx_clear(fb);
    draw_status(fb, v);
    draw_grid(fb);
    if (v->have_trace) {
        draw_trace(fb, v);
        draw_trigger_marks(fb, v);
        if (v->clipped) {
            gfx_text(fb, 1, (int)UI_PLOT_Y + 2, "CLIP", true);
        }
    }
    if (v->link_active) {
        gfx_text(fb, (int)LCD_WIDTH - 3 * GFX_CHAR_W - 4, (int)UI_PLOT_Y + 2, "USB", true);
    }
    draw_meas(fb, v);
}
