/**
 * @file    lcd_st75256.c
 *
 * ST75256 protocol notes (Sitronix ST75256 datasheet):
 *   - A0 = 0 marks a command byte, A0 = 1 marks parameter *and* display data.
 *   - Commands live in two "extension" sets selected with 0x30 (EXT1) and
 *     0x31 (EXT2).
 *   - 0x15 / 0x75 set the column / page window; 0x5C starts a RAM write and the
 *     address auto-increments inside the window, so one burst covers the frame.
 *
 * The init table below follows the controller datasheet's recommended power-up
 * order. Contrast (Vop), bias and duty depend on the glass and must be tuned
 * on the real module - see docs/bringup-checklist.md.
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
    DELAY(50),
    CMD(0xAE),                          /* display off                             */
    CMD(0x31),                          /* EXT2                                    */
    CMD(0xD7), DAT(0x9F),               /* disable auto-read (OTP)                 */
    CMD(0x32), DAT(0x00), DAT(0x01), DAT(0x03), /* analog: booster eff, bias 1/11 */
    CMD(0x51), DAT(0xFA),               /* booster level x10                       */
    CMD(0x30),                          /* EXT1                                    */
    CMD(0x20), DAT(0x0B),               /* power control: booster+reg+follower on  */
    DELAY(20),
    CMD(0x81), DAT(LCD_VOP_DEFAULT & 0x3Fu), DAT((LCD_VOP_DEFAULT >> 6) & 0x07u), /* Vop */
    CMD(0xCA), DAT(0x00), DAT(0x5F), DAT(0x00), /* display ctrl: CLD, duty 1/96, FR */
    CMD(0xF0), DAT(0x10),               /* monochrome mode (1 bit/pixel)           */
    CMD(0xBC), DAT(0x00),               /* data scan direction: column-major, normal */
    CMD(0x0C),                          /* data format: D0 = top pixel of a page   */
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
