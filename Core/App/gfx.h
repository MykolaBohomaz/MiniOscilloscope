/**
 * @file    gfx.h
 * @brief   Minimal 1 bpp graphics for the 192x96 ST75256 framebuffer.
 *
 * Memory layout matches the controller's page addressing so the buffer can be
 * streamed with a single DMA transfer: byte fb[page * LCD_WIDTH + x] holds
 * pixels (x, page*8 .. page*8+7), bit 0 = top row of the page.
 */
#ifndef GFX_H
#define GFX_H

#include <stdbool.h>
#include <stdint.h>
#include "scope_config.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { GFX_CLEAR = 0, GFX_SET = 1, GFX_XOR = 2 } gfx_op_t;

#define GFX_CHAR_W 6   /* 5 px glyph + 1 px spacing */
#define GFX_CHAR_H 8

/* Non-ASCII glyphs appended after '~' */
#define GLYPH_RISING   "\x7F"
#define GLYPH_FALLING  "\x80"

void gfx_clear(uint8_t *fb);
void gfx_pixel(uint8_t *fb, int x, int y, gfx_op_t op);
bool gfx_get(const uint8_t *fb, int x, int y);
void gfx_hline(uint8_t *fb, int x0, int x1, int y, gfx_op_t op);
void gfx_vline(uint8_t *fb, int x, int y0, int y1, gfx_op_t op);
void gfx_fill_rect(uint8_t *fb, int x, int y, int w, int h, gfx_op_t op);
/** Draw text; returns x after the last glyph. Unknown chars render as '?'. */
int  gfx_text(uint8_t *fb, int x, int y, const char *s, bool invert);

#ifdef __cplusplus
}
#endif
#endif /* GFX_H */
