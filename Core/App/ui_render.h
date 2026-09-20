/**
 * @file    ui_render.h
 * @brief   Draws one complete instrument screen into a framebuffer from a
 *          plain "view model". No hardware access: the same code renders the
 *          README screenshots on the host (tools/render_preview).
 *
 *   y  0..7   status line : RUN  100us/ _|1.65V AUTO Trig
 *   y 10..73  waveform    : 8 x 4 division grid, min/max trace, trigger marks
 *   y 77..84  measurements: P-P, AVG, frequency
 *   y 87..94  measurements: RMS, duty  |  selected auxiliary setting
 */
#ifndef UI_RENDER_H
#define UI_RENDER_H

#include <stdbool.h>
#include <stdint.h>
#include "acq_engine.h"
#include "scope_settings.h"
#include "ui_menu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const scope_settings_t *set;
    menu_item_t  sel;

    /* acquisition status */
    acq_state_t  acq_state;
    bool         last_forced;       /**< shown record came from AUTO timeout */
    bool         link_active;       /**< PC talked to us recently            */

    /* trace (LCD_WIDTH columns of min/max raw codes) */
    bool            have_trace;
    const uint16_t *col_min;
    const uint16_t *col_max;
    uint16_t        trig_col;       /**< x of the trigger point              */

    /* measurements, physical units */
    bool  meas_valid;
    bool  periodic;
    bool  clipped;
    float vpp_mv, vavg_mv, vrms_mv, freq_hz, duty_pct;
    float trig_level_mv;
} ui_view_t;

void ui_render(uint8_t *fb, const ui_view_t *v);

#ifdef __cplusplus
}
#endif
#endif /* UI_RENDER_H */
