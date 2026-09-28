/**
 * @file    lcd_st75256.c
 *
 * Everything below is from the Sitronix ST75256 datasheet v1.4 and the
 * EastRising ERM19296-1 module datasheet (rev 1.0):
 *   - A0 = 0 marks a command byte; every command *parameter* and all display
 *     data go out with A0 = 1 (all parameter rows in section 9.2 show A0 = 1).
 *   - Commands live in extension sets selected by 0x30 (EXT1) / 0x31 (EXT2).
 *   - 0x15 / 0x75 set the column / page window. 0x5C resets the address to
 *     (XS, YS); the column address then auto-increments, the page address
 *     increments when the column passes XE and wraps to YS past YE (9.2.7,
 *     9.2.8, 9.2.10). One DMA burst therefore covers a whole frame.
 *   - Serial timing (14.3): tSCYC >= 80 ns, so SCL <= 12.5 MHz; SDA is latched
 *     on the rising edge of SCL, which is SPI mode 0. The bring-up prescaler
 *     of /16 gives 5 MHz; /8 (10 MHz) is still inside the spec.
 *   - Reset (14.6): RSTB low >= 1 ms (tRW), reset completes within 1 ms (tR).
 *
 * The init sequence follows the power-on flow of section 10.1. Contrast (Vop)
 * is the one value that depends on the individual glass - see
 * docs/bringup-checklist.md.
 */
#include "lcd_st75256.h"
#include "board.h"
#include "scope_config.h"
#include "spi.h"

#define CMD(x)    (0x100u | (x))   /* table encoding: bit 8 = command byte */
#define DAT(x)    (x)
#define DELAY(ms) (0x200u | (ms))
#define END       0xFFFFu

static const uint16_t k_init_seq[] = {
    CMD(0x30),                          /* EXT1                                    */
    CMD(0x94),                          /* sleep out                               */
    DELAY(50), CMD(0xAE),                          /* display off                             */
    CMD(0x31),                          /* EXT2                                    */
    CMD(0xD7), DAT(0x9F),               /* disable auto-read (OTP)                 */
    /* 9.2.32 analog circuit set: reserved 0, booster efficiency 6 kHz (default),
     * LCD bias 1/11 - the bias the ERM19296-1 module datasheet specifies. */
    CMD(0x32), DAT(0x00), DAT(0x01), DAT(0x03),
    /* 9.2.33 booster level: D0 = BST, 1 = x10 (default), 0 = x8. */
    CMD(0x51), DAT(0xFB),
    CMD(0x30),                          /* EXT1                                    */
    CMD(0x20), DAT(0x0B),               /* power control: booster+reg+follower on  */
    DELAY(20),
    /* 9.2.21 Vop: V0 = 3.6 V + Vop[8:0] x 0.04 V, split 6 bits then 3 bits. */
    CMD(0x81), DAT(LCD_VOP_DEFAULT & 0x3Fu), DAT((LCD_VOP_DEFAULT >> 6) & 0x07u),
    /* 9.2.25 display control: CLD = 0 (no clock division), DT = duty - 1 = 95
     * for this 1/96-duty module, LF/FI = 0 (frame inversion, the default). */
    CMD(0xCA), DAT(0x00), DAT(0x5F), DAT(0x00),
    /* 9.2.28 display mode: DM = 0 -> monochrome, 1 bit per pixel. */
    CMD(0xF0), DAT(0x10),
    /* 9.2.9 data scan direction: MV = MX = MY = 0 (column direction, normal).
     * Change to 0x03 if the image comes out rotated 180 degrees. */
    CMD(0xBC), DAT(0x00),
    /* 9.2.27 data format: DO = 1 -> "LSB on top", D0 is the topmost pixel of a
     * page, which is what gfx.c writes. 0x08 would flip every 8-pixel band. */
    CMD(0x0C),
    CMD(0xA6),                          /* normal (non-inverted) display           */
    CMD(0xAF),                          /* display on                              */
    END
};

static volatile bool     s_busy;
static volatile uint32_t s_frames;
static volatile uint32_t s_errors;

static void spi_write(const uint8_t *data, uint16_t n)
{
    (void)HAL_SPI_Transmit(&hspi1, (uint8_t *)data, n, 10u);
}

static void send_cmd(uint8_t c)
{
    LCD_A0_CMD();
    spi_write(&c, 1u);
}

static void send_data(const uint8_t *d, uint16_t n)
{
    LCD_A0_DATA();
    spi_write(d, n);
}

static void set_window_and_write(void)
{
    const uint8_t col[2]  = { LCD_COL_OFFSET, LCD_COL_OFFSET + LCD_WIDTH - 1u };
    const uint8_t page[2] = { LCD_PAGE_OFFSET, LCD_PAGE_OFFSET + LCD_PAGES - 1u };
    send_cmd(0x30);
    send_cmd(0x15); send_data(col, 2u);
    send_cmd(0x75); send_data(page, 2u);
    send_cmd(0x5C);
}

void lcd_init(void)
{
    LCD_CS_HIGH();
    LCD_RST_LOW();
    HAL_Delay(5);
    LCD_RST_HIGH();
    HAL_Delay(120);            /* reset release to first command */

    LCD_CS_LOW();
    for (const uint16_t *p = k_init_seq; *p != END; p++) {
        if (*p & 0x200u) {
            LCD_CS_HIGH();
            HAL_Delay(*p & 0xFFu);
            LCD_CS_LOW();
        } else if (*p & 0x100u) {
            send_cmd((uint8_t)*p);
        } else {
            const uint8_t d = (uint8_t)*p;
            send_data(&d, 1u);
        }
    }
    LCD_CS_HIGH();

    /* Clear RAM (blocking, boot only). */
    static const uint8_t zeros[LCD_WIDTH] = {0};
    LCD_CS_LOW();
    set_window_and_write();
    LCD_A0_DATA();
    for (uint32_t pg = 0; pg < LCD_PAGES; pg++) {
        spi_write(zeros, LCD_WIDTH);
    }
    LCD_CS_HIGH();
}

bool lcd_busy(void)
{
    return s_busy;
}

bool lcd_flush_async(const uint8_t *fb)
{
    if (s_busy) {
        return false;
    }
    s_busy = true;
    LCD_CS_LOW();
    set_window_and_write();
    LCD_A0_DATA();
    if (HAL_SPI_Transmit_DMA(&hspi1, (uint8_t *)fb, LCD_FB_BYTES) != HAL_OK) {
        LCD_CS_HIGH();
        s_errors++;
        s_busy = false;
        return false;
    }
    return true;
}

void lcd_set_contrast(uint16_t vop)
{
    if (s_busy) {
        return;
    }
    const uint8_t p[2] = { (uint8_t)(vop & 0x3Fu), (uint8_t)((vop >> 6) & 0x07u) };
    LCD_CS_LOW();
    send_cmd(0x30);
    send_cmd(0x81);
    send_data(p, 2u);
    LCD_CS_HIGH();
}

uint32_t lcd_frames_sent(void)
{
    return s_frames;
}

void lcd_isr_tx_complete(void)
{
    LCD_CS_HIGH();
    s_frames++;
    s_busy = false;
}

void lcd_isr_error(void)
{
    LCD_CS_HIGH();
    s_errors++;
    s_busy = false;
}
