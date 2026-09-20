/* Number formatting, menu logic, graphics primitives and the full renderer. */
#include <string.h>
#include "unit.h"
#include "gfx.h"
#include "scope_config.h"
#include "scope_settings.h"
#include "timebase.h"
#include "ui_format.h"
#include "ui_menu.h"
#include "ui_render.h"

extern const uint8_t g_font5x7[][5];

static void test_fmt_eng(void)
{
    char b[24];
    fmt_eng(b, sizeof b, 0.001234f, "V");  CHECK_STR(b, "1.23mV");
    fmt_eng(b, sizeof b, 1000.0f, "Hz");   CHECK_STR(b, "1.00kHz");
    fmt_eng(b, sizeof b, -12.34f, "V");    CHECK_STR(b, "-12.3V");
    fmt_eng(b, sizeof b, 999.6f, "Hz");    CHECK_STR(b, "1.00kHz");   /* carry */
    fmt_eng(b, sizeof b, 9.996f, "V");     CHECK_STR(b, "10.0V");
    fmt_eng(b, sizeof b, 0.0f, "V");       CHECK_STR(b, "0.00V");
    fmt_eng(b, sizeof b, 3.3f, "V");       CHECK_STR(b, "3.30V");
    fmt_eng(b, sizeof b, 250e3f, "Hz");    CHECK_STR(b, "250kHz");
    fmt_eng(b, sizeof b, 5e6f, "S/s");     CHECK_STR(b, "5.00MS/s");
    fmt_eng(b, sizeof b, NAN, "V");        CHECK_STR(b, "---");
    fmt_pct(b, sizeof b, 49.96f);          CHECK_STR(b, "50.0%");
    fmt_pct(b, sizeof b, 3.04f);           CHECK_STR(b, "3.0%");
}

static void test_menu(void)
{
    scope_settings_t s;
    scope_settings_default(&s);
    CHECK_EQ_U(ui_menu_adjust(&s, MENU_TIMEBASE, -100), CHG_TIMEBASE);
    CHECK_EQ_U(s.tb_index, 0);
    CHECK_EQ_U(ui_menu_adjust(&s, MENU_TIMEBASE, -1), CHG_NONE);   /* clamped */
    CHECK_EQ_U(ui_menu_adjust(&s, MENU_TRIG_LEVEL, 1000), CHG_TRIGGER);
    CHECK_EQ_U(s.trig_level, ADC_FULL_SCALE);
    ui_menu_adjust(&s, MENU_PRETRIG, 100);
    CHECK_EQ_U(s.pretrig_pct, 90);
    /* generator walk: OFF -> SINE f0 ... -> TRI f2 */
    CHECK_EQ_U(s.gen_wave, GEN_OFF);
    ui_menu_adjust(&s, MENU_GEN, 1);
    CHECK(s.gen_wave == GEN_SINE && s.gen_freq == 0);
    ui_menu_adjust(&s, MENU_GEN, 100);
    CHECK(s.gen_wave == GEN_TRIANGLE && s.gen_freq == GEN_FREQ_COUNT - 1);
    ui_menu_adjust(&s, MENU_GEN, -100);
    CHECK(s.gen_wave == GEN_OFF);
    /* sanitize repairs garbage (e.g. from a foreign flash record) */
    memset(&s, 0xFF, sizeof s);
    scope_settings_sanitize(&s);
    CHECK(s.tb_index < g_timebase_count);
    CHECK(s.pretrig_pct <= 90 && s.backlight <= 10);
}

static void test_gfx(void)
{
    static uint8_t fb[LCD_FB_BYTES];
    CHECK_EQ_U(LCD_FB_BYTES, 2304);
    gfx_clear(fb);
    gfx_pixel(fb, 5, 9, GFX_SET);
    CHECK(gfx_get(fb, 5, 9));
    CHECK_EQ_U(fb[1 * LCD_WIDTH + 5], 0x02);                   /* page 1, bit 1 */
    gfx_pixel(fb, -1, 200, GFX_SET);                           /* clipped, no crash */
    gfx_vline(fb, 10, 3, 20, GFX_SET);
    for (int y = 0; y < 30; y++) CHECK(gfx_get(fb, 10, y) == (y >= 3 && y <= 20));
    gfx_vline(fb, 10, 3, 20, GFX_XOR);
    CHECK(!gfx_get(fb, 10, 12));
    const int x = gfx_text(fb, 0, 40, "A", false);
    CHECK_EQ_U(x, GFX_CHAR_W);
    CHECK(gfx_get(fb, 0, 41));                                 /* 'A' left stroke */
    /* font covers 0x20..0x80 exactly */
    CHECK_EQ_U(g_font5x7[0x80 - 0x20][2], 0x7F);
}

static void test_render_smoke(void)
{
    static uint8_t fb[LCD_FB_BYTES];
    static uint16_t lo[LCD_WIDTH], hi[LCD_WIDTH];
    for (uint32_t c = 0; c < LCD_WIDTH; c++) {
        lo[c] = (uint16_t)(2048 + 1500 * sin(c * 0.1));
        hi[c] = (uint16_t)(lo[c] + 20);
    }
    scope_settings_t s;
    scope_settings_default(&s);
    ui_view_t v = {0};
    v.set = &s;
    v.have_trace = true;
    v.col_min = lo; v.col_max = hi;
    v.trig_col = 96;
    v.meas_valid = true;
    v.periodic = true;
    v.vpp_mv = 2400; v.vavg_mv = 1650; v.vrms_mv = 1800; v.freq_hz = 1000; v.duty_pct = 50;
    for (int sel = 0; sel < MENU_COUNT; sel++) {
        v.sel = (menu_item_t)sel;
        ui_render(fb, &v);
    }
    uint32_t lit = 0;
    for (uint32_t i = 0; i < LCD_FB_BYTES; i++) lit += (uint32_t)__builtin_popcount(fb[i]);
    CHECK(lit > 500 && lit < LCD_WIDTH * LCD_HEIGHT / 2);
}

int main(void)
{
    RUN(test_fmt_eng);
    RUN(test_menu);
    RUN(test_gfx);
    RUN(test_render_smoke);
    UNIT_REPORT();
}
