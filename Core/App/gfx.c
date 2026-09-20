/**
 * @file    gfx.c
 */
#include "gfx.h"
#include <string.h>

extern const uint8_t g_font5x7[][5];
#define FONT_FIRST 0x20
#define FONT_LAST  0x80

void gfx_clear(uint8_t *fb)
{
    memset(fb, 0, LCD_FB_BYTES);
}

void gfx_pixel(uint8_t *fb, int x, int y, gfx_op_t op)
{
    if (x < 0 || y < 0 || x >= (int)LCD_WIDTH || y >= (int)LCD_HEIGHT) {
        return;
    }
    uint8_t *b = &fb[(uint32_t)(y >> 3) * LCD_WIDTH + (uint32_t)x];
    const uint8_t m = (uint8_t)(1u << (y & 7));
    switch (op) {
    case GFX_SET:   *b |= m;  break;
    case GFX_CLEAR: *b &= (uint8_t)~m; break;
    default:        *b ^= m;  break;
    }
}

bool gfx_get(const uint8_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= (int)LCD_WIDTH || y >= (int)LCD_HEIGHT) {
        return false;
    }
    return (fb[(uint32_t)(y >> 3) * LCD_WIDTH + (uint32_t)x] >> (y & 7)) & 1u;
}

void gfx_hline(uint8_t *fb, int x0, int x1, int y, gfx_op_t op)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    for (int x = x0; x <= x1; x++) {
        gfx_pixel(fb, x, y, op);
    }
}

void gfx_vline(uint8_t *fb, int x, int y0, int y1, gfx_op_t op)
{
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    if (x < 0 || x >= (int)LCD_WIDTH) {
        return;
    }
    if (y0 < 0) { y0 = 0; }
    if (y1 >= (int)LCD_HEIGHT) { y1 = (int)LCD_HEIGHT - 1; }
    /* Byte-at-a-time vertical span: this is the waveform hot path. */
    for (int y = y0; y <= y1;) {
        const int page = y >> 3;
        const int bit0 = y & 7;
        const int last = (y1 >> 3) == page ? (y1 & 7) : 7;
        const uint8_t m = (uint8_t)(((0xFFu << bit0) & (0xFFu >> (7 - last))) & 0xFFu);
        uint8_t *b = &fb[(uint32_t)page * LCD_WIDTH + (uint32_t)x];
        if (op == GFX_SET) { *b |= m; } else if (op == GFX_CLEAR) { *b &= (uint8_t)~m; } else { *b ^= m; }
        y = (page + 1) * 8;
    }
}

void gfx_fill_rect(uint8_t *fb, int x, int y, int w, int h, gfx_op_t op)
{
    for (int i = 0; i < w; i++) {
        gfx_vline(fb, x + i, y, y + h - 1, op);
    }
}

int gfx_text(uint8_t *fb, int x, int y, const char *s, bool invert)
{
    for (; *s != '\0'; s++) {
        unsigned c = (unsigned char)*s;
        if (c < FONT_FIRST || c > FONT_LAST) {
            c = '?';
        }
        const uint8_t *g = g_font5x7[c - FONT_FIRST];
        for (int col = 0; col < GFX_CHAR_W; col++) {
            const uint8_t bits = (col < 5) ? g[col] : 0u;
            for (int row = 0; row < GFX_CHAR_H; row++) {
                const bool on = ((bits >> row) & 1u) != 0u;
                gfx_pixel(fb, x + col, y + row, (on != invert) ? GFX_SET : GFX_CLEAR);
            }
        }
        x += GFX_CHAR_W;
    }
    return x;
}
