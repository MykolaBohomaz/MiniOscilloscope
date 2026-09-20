/**
 * @file    ui_menu.h
 * @brief   Encoder + button menu logic (pure state, no hardware, no drawing).
 *
 *   MENU      : select next parameter
 *   encoder   : adjust selected parameter
 *   ENC_SW    : "auto level" - put the trigger level at the signal mid-point
 *   RUN/STOP  : toggle continuous acquisition
 *   SINGLE    : arm one acquisition and hold it
 */
#ifndef UI_MENU_H
#define UI_MENU_H

#include <stdint.h>
#include "scope_settings.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MENU_TIMEBASE = 0,
    MENU_TRIG_LEVEL,
    MENU_EDGE,
    MENU_MODE,
    MENU_PRETRIG,
    MENU_GEN,
    MENU_BACKLIGHT,
    MENU_COUNT
} menu_item_t;

/** Bit flags telling the application which subsystem must be reconfigured. */
enum {
    CHG_NONE      = 0,
    CHG_TIMEBASE  = 1u << 0,   /* stop/reconfigure ADC + TIM6                */
    CHG_TRIGGER   = 1u << 1,   /* level/edge/mode/pre-trigger, re-arm         */
    CHG_RUN       = 1u << 2,   /* run/stop/single                            */
    CHG_GEN       = 1u << 3,   /* DAC self-test generator                    */
    CHG_BACKLIGHT = 1u << 4,
};

#define UI_TRIG_STEP_CODES 32u   /* per encoder detent */

/** Apply @p detents of encoder rotation to item @p sel. @return CHG_* flags. */
uint32_t ui_menu_adjust(scope_settings_t *s, menu_item_t sel, int detents);

const char *ui_menu_name(menu_item_t m);

#ifdef __cplusplus
}
#endif
#endif /* UI_MENU_H */
