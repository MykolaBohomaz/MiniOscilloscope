/**
 * @file    lcd_st75256.h
 * @brief   ERM19296FSF-1 (192x96, Sitronix ST75256) over 4-wire SPI + DMA.
 *
 * Only the tiny window/command preamble of each frame is sent with blocking
 * SPI (7 bytes, ~12 us at 5 MHz). The 2304-byte framebuffer goes out in one
 * DMA transfer; completion is signalled from the SPI TX-complete ISR, which
 * only releases chip-select and clears the busy flag.
 */
#ifndef LCD_ST75256_H
#define LCD_ST75256_H

#include <stdbool.h>
#include <stdint.h>

/* ---- module-dependent parameters (VERIFY on the actual panel) -------------- */
/** First controller column wired to the glass (ST75256 has 256 columns). */
#ifndef LCD_COL_OFFSET
#define LCD_COL_OFFSET   0u
#endif
/** First controller page wired to the glass (ST75256 has 20 pages / 160 COMs). */
#ifndef LCD_PAGE_OFFSET
#define LCD_PAGE_OFFSET  0u
#endif
/** Contrast (Vop) default, 9-bit value split over two parameter bytes. */
#ifndef LCD_VOP_DEFAULT
#define LCD_VOP_DEFAULT  0x0118u
#endif

void lcd_init(void);
bool lcd_busy(void);
/** Start an asynchronous full-frame update. @return false if still busy. */
bool lcd_flush_async(const uint8_t *fb);
void lcd_set_contrast(uint16_t vop);
uint32_t lcd_frames_sent(void);

/* ISR hooks */
void lcd_isr_tx_complete(void);
void lcd_isr_error(void);

#endif /* LCD_ST75256_H */
