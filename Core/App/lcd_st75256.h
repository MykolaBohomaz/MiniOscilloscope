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

/* ---- module-dependent parameters ------------------------------------------- */
/* The ST75256 drives 256 columns x 162 lines; the ERM19296-1 glass uses
 * 192 x 96 of that. The offsets below say where the glass starts. Both are 0
 * unless bring-up shows the image shifted. */
/** First controller column wired to the glass (ST75256 has 256 columns). */
#ifndef LCD_COL_OFFSET
#define LCD_COL_OFFSET   0u
#endif
/** First controller page wired to the glass (ST75256 has 20 pages / 160 COMs). */
#ifndef LCD_PAGE_OFFSET
#define LCD_PAGE_OFFSET  0u
#endif
/**
 * Contrast: 9-bit Vop code. V0 = 3.6 V + code x 0.04 V (datasheet 9.2.21), so
 * 0x118 (280) = 14.8 V. Usable range per the datasheet is 7.00 V .. 18.00 V
 * (codes 0x55 .. 0x168). This is the value to tune per panel.
 */
#ifndef LCD_VOP_DEFAULT
#define LCD_VOP_DEFAULT  0x00D2u      /* 12.0 V */
#endif
/** Datasheet's usable window: 7.00 V .. 18.00 V. */
#define LCD_VOP_MIN      0x0055u
#define LCD_VOP_MAX      0x0168u
/** Volts <-> code helpers (V0 = 3.6 V + code x 0.04 V). */
#define LCD_VOP_FROM_MV(mv)  (uint16_t)(((mv) - 3600) / 40)

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
